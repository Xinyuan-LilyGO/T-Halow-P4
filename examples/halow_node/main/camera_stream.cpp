/*
 * Camera H.264 streaming for the HaLow node.
 *
 * Pipeline (all hardware): CSI sensor -> ISP (YUV420) -> H.264 encoder, via
 * the esp_video V4L2 layer -- /dev/video0 capture feeds the /dev/video11 M2M
 * encoder exactly as in esp_video's sd_card example. The Annex-B output is
 * written verbatim to a single TCP client; TCP backpressure over the HaLow
 * link is the only rate control beyond the encoder's own bitrate target.
 *
 * The pipeline is built on client connect and torn all the way back down on
 * disconnect, because it is not affordable to hold it open: measured on this
 * board, a resident pipeline leaves ~1.3 kB of internal DMA heap, which
 * starves esp_hosted's SDIO receive buffers until the uplink stalls and the
 * stall watchdog restarts the host. Idle (the wildlife camera's normal state)
 * therefore costs no camera memory at all; the sensor is probed once at boot
 * so we know whether to listen at all. Client connect to first frame is ~2 s.
 *
 * Board specifics (from examples/camera_display): the SGM38121 PMIC at 0x28
 * on I2C1 (SDA 7 / SCL 8) powers the sensor rails, the sensor SCCB shares
 * that same bus, and chip LDO channel 3 at 1.83 V feeds the MIPI PHY. The
 * camera module carries its own 24 MHz oscillator, so no MCLK from the P4.
 *
 * License: GPL 3.0
 */
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_ldo_regulator.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
/*
 * lwip/sockets.h must come first: it redefines _IO/_IOR/_IOW with the BSD
 * encoding, and VIDIOC_* expand those macros at the point of use. Included
 * the other way round, VIDIOC_STREAMON/STREAMOFF (the only V4L2 calls built
 * on plain _IOW -- everything else uses _IOWR, which lwIP leaves alone) come
 * out as BSD ioctl numbers and the driver rejects them with EINVAL, while
 * the rest of the pipeline configures perfectly. The static_assert below
 * fails the build rather than let that come back.
 */
#include "lwip/sockets.h"
#include "linux/videodev2.h"
#include "esp_video_init.h"
#include "esp_video_device.h"
#include "sdkconfig.h"

static_assert(VIDIOC_STREAMON == 0x40045612,
              "VIDIOC_STREAMON is not the Linux ioctl number -- _IOW has been "
              "redefined (see the include-order note above)");

#include "camera_stream.h"

static const char *TAG = "cam_stream";

#define CAM_I2C_PORT I2C_NUM_1
#define CAM_I2C_SDA 7
#define CAM_I2C_SCL 8
#define SGM38121_ADDR 0x28
#define MIPI_LDO_CHANNEL 3
#define MIPI_LDO_MV 1830

#define CAP_BUFFER_COUNT 2
#define SKIP_STARTUP_FRAMES 2

static int s_cap_fd = -1;
static int s_m2m_fd = -1;
static uint8_t *s_cap_buf[CAP_BUFFER_COUNT];
static size_t s_cap_buf_len[CAP_BUFFER_COUNT];
static uint8_t *s_enc_buf;
static size_t s_enc_buf_len;
static esp_video_init_csi_config_t s_csi_config;
static esp_video_init_config_t s_video_config;

static bool s_sensor_present = false;
static bool s_streaming = false;

extern "C" const char *camera_stream_state(void)
{
    return !s_sensor_present ? "absent" : (s_streaming ? "streaming" : "idle");
}

/* Internal DMA-capable heap -- the pool esp_hosted's SDIO driver competes for. */
static void log_dma_heap(const char *when)
{
    ESP_LOGI(TAG, "heap %s: dma %u/%u, internal %u, spiram %u", when,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

// ---------------------------------------------------------------------------
// Sensor power: SGM38121 rails, then the P4's own LDO for the MIPI PHY.
// ---------------------------------------------------------------------------

static esp_err_t sgm38121_power_up(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = SGM38121_ADDR;
    dev_cfg.scl_speed_hz = 100000;

    i2c_master_dev_handle_t dev;
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    if (err != ESP_OK)
    {
        return err;
    }

    /* Register map and rail settings lifted from examples/camera_display:
     * DVDD1 1.50 V, AVDD1 1.70 V, AVDD2 3.00 V, then enable those three. */
    struct
    {
        uint8_t reg;
        uint8_t val;
    } rails[] = {
        {0x03, (1500 - 504) / 8},  /* DVDD1 */
        {0x05, (1700 - 1348) / 8}, /* AVDD1 */
        {0x06, (3000 - 1348) / 8}, /* AVDD2 */
    };

    uint8_t id_reg = 0x00, id = 0;
    err = i2c_master_transmit_receive(dev, &id_reg, 1, &id, 1, 100);
    if (err != ESP_OK || id != 0x80)
    {
        ESP_LOGW(TAG, "SGM38121 not answering (err %d id 0x%02x)", err, id);
        i2c_master_bus_rm_device(dev);
        return err != ESP_OK ? err : ESP_ERR_NOT_FOUND;
    }

    for (size_t i = 0; i < sizeof(rails) / sizeof(rails[0]) && err == ESP_OK; i++)
    {
        uint8_t frame[2] = {rails[i].reg, rails[i].val};
        err = i2c_master_transmit(dev, frame, 2, 100);
    }
    if (err == ESP_OK)
    {
        uint8_t en_reg = 0x0e, en = 0;
        err = i2c_master_transmit_receive(dev, &en_reg, 1, &en, 1, 100);
        if (err == ESP_OK)
        {
            uint8_t frame[2] = {en_reg, (uint8_t)(en | 0x0d)}; /* DVDD1|AVDD1|AVDD2 */
            err = i2c_master_transmit(dev, frame, 2, 100);
        }
    }

    i2c_master_bus_rm_device(dev);
    return err;
}

// ---------------------------------------------------------------------------
// V4L2 pipeline: buffers are allocated once here; each client session only
// re-queues them and toggles STREAMON/STREAMOFF.
// ---------------------------------------------------------------------------

static esp_err_t set_codec_control(int fd, uint32_t id, int32_t value)
{
    struct v4l2_ext_controls controls;
    struct v4l2_ext_control control[1];

    memset(&controls, 0, sizeof(controls));
    memset(control, 0, sizeof(control));
    controls.ctrl_class = V4L2_CID_CODEC_CLASS;
    controls.count = 1;
    controls.controls = control;
    control[0].id = id;
    control[0].value = value;

    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls) != 0)
    {
        ESP_LOGW(TAG, "failed to set codec control %" PRIu32, id);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t pipeline_setup(void)
{
    struct v4l2_format format;
    struct v4l2_requestbuffers req;
    struct v4l2_buffer buf;
    uint32_t width, height;

    s_cap_fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
    if (s_cap_fd < 0)
    {
        ESP_LOGE(TAG, "open %s failed", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
        return ESP_FAIL;
    }

    /* Default frame size comes from the sensor mode chosen in menuconfig. */
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_cap_fd, VIDIOC_G_FMT, &format) != 0)
    {
        return ESP_FAIL;
    }
    width = format.fmt.pix.width;
    height = format.fmt.pix.height;

    /* Capture YUV420 from the ISP -- the layout the H.264 encoder consumes. */
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
    if (ioctl(s_cap_fd, VIDIOC_S_FMT, &format) != 0)
    {
        ESP_LOGE(TAG, "set YUV420 %" PRIu32 "x%" PRIu32 " failed", width, height);
        return ESP_FAIL;
    }

    memset(&req, 0, sizeof(req));
    req.count = CAP_BUFFER_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(s_cap_fd, VIDIOC_REQBUFS, &req) != 0)
    {
        return ESP_FAIL;
    }
    for (int i = 0; i < CAP_BUFFER_COUNT; i++)
    {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(s_cap_fd, VIDIOC_QUERYBUF, &buf) != 0)
        {
            return ESP_FAIL;
        }
        s_cap_buf[i] = (uint8_t *)mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                       MAP_SHARED, s_cap_fd, buf.m.offset);
        if (s_cap_buf[i] == NULL)
        {
            return ESP_FAIL;
        }
        s_cap_buf_len[i] = buf.length;
    }

    s_m2m_fd = open(ESP_VIDEO_H264_DEVICE_NAME, O_RDONLY);
    if (s_m2m_fd < 0)
    {
        ESP_LOGE(TAG, "open %s failed", ESP_VIDEO_H264_DEVICE_NAME);
        return ESP_FAIL;
    }

    set_codec_control(s_m2m_fd, V4L2_CID_MPEG_VIDEO_H264_I_PERIOD, CONFIG_NODE_H264_I_PERIOD);
    set_codec_control(s_m2m_fd, V4L2_CID_MPEG_VIDEO_BITRATE, CONFIG_NODE_H264_BITRATE);
    set_codec_control(s_m2m_fd, V4L2_CID_MPEG_VIDEO_H264_MIN_QP, CONFIG_NODE_H264_MIN_QP);
    set_codec_control(s_m2m_fd, V4L2_CID_MPEG_VIDEO_H264_MAX_QP, CONFIG_NODE_H264_MAX_QP);

    /* Encoder input side: fed by pointer straight from the capture buffers. */
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
    if (ioctl(s_m2m_fd, VIDIOC_S_FMT, &format) != 0)
    {
        return ESP_FAIL;
    }
    memset(&req, 0, sizeof(req));
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    req.memory = V4L2_MEMORY_USERPTR;
    if (ioctl(s_m2m_fd, VIDIOC_REQBUFS, &req) != 0)
    {
        return ESP_FAIL;
    }

    /* Encoder output side: one buffer of encoded NALs. */
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_H264;
    if (ioctl(s_m2m_fd, VIDIOC_S_FMT, &format) != 0)
    {
        return ESP_FAIL;
    }
    memset(&req, 0, sizeof(req));
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(s_m2m_fd, VIDIOC_REQBUFS, &req) != 0)
    {
        return ESP_FAIL;
    }
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;
    if (ioctl(s_m2m_fd, VIDIOC_QUERYBUF, &buf) != 0)
    {
        return ESP_FAIL;
    }
    s_enc_buf = (uint8_t *)mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                MAP_SHARED, s_m2m_fd, buf.m.offset);
    if (s_enc_buf == NULL)
    {
        return ESP_FAIL;
    }
    s_enc_buf_len = buf.length;

    ESP_LOGI(TAG, "pipeline ready: %" PRIu32 "x%" PRIu32 " YUV420 -> H.264 @ %d bit/s GOP %d",
             width, height, CONFIG_NODE_H264_BITRATE, CONFIG_NODE_H264_I_PERIOD);
    return ESP_OK;
}

/* Give every byte back: the SDIO uplink needs the internal DMA heap we hold. */
static void pipeline_teardown(void)
{
    struct v4l2_requestbuffers req;

    if (s_m2m_fd >= 0)
    {
        if (s_enc_buf != NULL)
        {
            munmap(s_enc_buf, s_enc_buf_len);
            s_enc_buf = NULL;
        }
        memset(&req, 0, sizeof(req));
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        ioctl(s_m2m_fd, VIDIOC_REQBUFS, &req);

        memset(&req, 0, sizeof(req));
        req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        req.memory = V4L2_MEMORY_USERPTR;
        ioctl(s_m2m_fd, VIDIOC_REQBUFS, &req);
        close(s_m2m_fd);
        s_m2m_fd = -1;
    }

    if (s_cap_fd >= 0)
    {
        for (int i = 0; i < CAP_BUFFER_COUNT; i++)
        {
            if (s_cap_buf[i] != NULL)
            {
                munmap(s_cap_buf[i], s_cap_buf_len[i]);
                s_cap_buf[i] = NULL;
            }
        }
        memset(&req, 0, sizeof(req));
        req.count = 0;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        ioctl(s_cap_fd, VIDIOC_REQBUFS, &req);
        close(s_cap_fd);
        s_cap_fd = -1;
    }
}

static bool session_start(void)
{
    struct v4l2_buffer buf;
    int type;

    for (int i = 0; i < CAP_BUFFER_COUNT; i++)
    {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(s_cap_fd, VIDIOC_QBUF, &buf) != 0)
        {
            ESP_LOGE(TAG, "cap QBUF[%d] failed: %s", i, strerror(errno));
            return false;
        }
    }
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;
    if (ioctl(s_m2m_fd, VIDIOC_QBUF, &buf) != 0)
    {
        ESP_LOGE(TAG, "enc QBUF failed: %s", strerror(errno));
        return false;
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_m2m_fd, VIDIOC_STREAMON, &type) != 0)
    {
        ESP_LOGE(TAG, "enc STREAMON(cap) failed: %s", strerror(errno));
        return false;
    }
    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (ioctl(s_m2m_fd, VIDIOC_STREAMON, &type) != 0)
    {
        ESP_LOGE(TAG, "enc STREAMON(out) failed: %s", strerror(errno));
        return false;
    }
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_cap_fd, VIDIOC_STREAMON, &type) != 0)
    {
        ESP_LOGE(TAG, "cap STREAMON failed: %s", strerror(errno));
        return false;
    }

    /* Let exposure settle so the first delivered GOP is watchable. */
    for (int i = 0; i < SKIP_STARTUP_FRAMES; i++)
    {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(s_cap_fd, VIDIOC_DQBUF, &buf) != 0)
        {
            ESP_LOGE(TAG, "warmup DQBUF failed: %s", strerror(errno));
            return false;
        }
        if (ioctl(s_cap_fd, VIDIOC_QBUF, &buf) != 0)
        {
            ESP_LOGE(TAG, "warmup QBUF failed: %s", strerror(errno));
            return false;
        }
    }
    return true;
}

static void session_stop(void)
{
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(s_cap_fd, VIDIOC_STREAMOFF, &type);
    type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ioctl(s_m2m_fd, VIDIOC_STREAMOFF, &type);
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(s_m2m_fd, VIDIOC_STREAMOFF, &type);
}

/* One camera frame through the encoder; *out_len = encoded bytes. */
static bool encode_frame(size_t *out_len)
{
    struct v4l2_buffer cap_buf, m2m_out, m2m_cap;

    memset(&cap_buf, 0, sizeof(cap_buf));
    cap_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cap_buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(s_cap_fd, VIDIOC_DQBUF, &cap_buf) != 0)
    {
        return false;
    }

    memset(&m2m_out, 0, sizeof(m2m_out));
    m2m_out.index = 0;
    m2m_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    m2m_out.memory = V4L2_MEMORY_USERPTR;
    m2m_out.m.userptr = (unsigned long)s_cap_buf[cap_buf.index];
    m2m_out.length = cap_buf.bytesused;
    if (ioctl(s_m2m_fd, VIDIOC_QBUF, &m2m_out) != 0)
    {
        return false;
    }

    memset(&m2m_cap, 0, sizeof(m2m_cap));
    m2m_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    m2m_cap.memory = V4L2_MEMORY_MMAP;
    if (ioctl(s_m2m_fd, VIDIOC_DQBUF, &m2m_cap) != 0)
    {
        return false;
    }

    if (ioctl(s_cap_fd, VIDIOC_QBUF, &cap_buf) != 0 ||
        ioctl(s_m2m_fd, VIDIOC_DQBUF, &m2m_out) != 0)
    {
        return false;
    }

    *out_len = m2m_cap.bytesused;
    return true;
}

/* Re-arm the encoder output buffer after its content has been sent. */
static bool encode_frame_done(void)
{
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.index = 0;
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    return ioctl(s_m2m_fd, VIDIOC_QBUF, &buf) == 0;
}

// ---------------------------------------------------------------------------
// TCP server: one client, stream until it goes away.
// ---------------------------------------------------------------------------

static bool send_all(int sock, const uint8_t *data, size_t len)
{
    while (len > 0)
    {
        int n = send(sock, data, len, 0);
        if (n <= 0)
        {
            return false;
        }
        data += n;
        len -= (size_t)n;
    }
    return true;
}

static void serve_client(int client)
{
    struct timeval tmo = {.tv_sec = 10, .tv_usec = 0};
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tmo, sizeof(tmo));

#if CONFIG_NODE_STREAM_STOPS_WIFI
    /* There is not enough internal DMA-capable RAM on this board to run the
     * capture/encode pipeline and the 2.4 GHz side at once: with WiFi up,
     * STREAMON has ~2 kB to work with and fails. Park WiFi for the session
     * and give it back afterwards -- while someone is watching, the node is
     * reachable over HaLow anyway (through the gateway's portal forward). */
    esp_wifi_stop();
    log_dma_heap("wifi parked");
#endif

    if (pipeline_setup() != ESP_OK)
    {
        ESP_LOGE(TAG, "pipeline bring-up failed, dropping client");
        pipeline_teardown();
#if CONFIG_NODE_STREAM_STOPS_WIFI
        esp_wifi_start();
#endif
        return;
    }
    log_dma_heap("with pipeline up");
    s_streaming = true;

    if (!session_start())
    {
        ESP_LOGE(TAG, "session start failed");
        session_stop();
        pipeline_teardown();
#if CONFIG_NODE_STREAM_STOPS_WIFI
        esp_wifi_start();
#endif
        return;
    }

    uint32_t frames = 0;
    uint64_t bytes = 0;
    int64_t t0 = esp_timer_get_time();

    while (true)
    {
        size_t len = 0;
        if (!encode_frame(&len))
        {
            ESP_LOGE(TAG, "pipeline error, dropping client");
            break;
        }
        if (frames == 0 && len >= 5)
        {
            ESP_LOGI(TAG, "first NAL: %02x %02x %02x %02x %02x (len %u)",
                     s_enc_buf[0], s_enc_buf[1], s_enc_buf[2], s_enc_buf[3],
                     s_enc_buf[4], (unsigned)len);
        }
        bool sent = send_all(client, s_enc_buf, len);
        if (!encode_frame_done() || !sent)
        {
            break;
        }
        frames++;
        bytes += len;
        if ((frames % 100) == 0)
        {
            int64_t dt = esp_timer_get_time() - t0;
            ESP_LOGI(TAG, "streamed %" PRIu32 " frames, %.1f fps, %.0f kbit/s",
                     frames, frames * 1e6 / dt, bytes * 8e3 / dt);
        }
    }

    s_streaming = false;
    session_stop();
    pipeline_teardown();
    ESP_LOGI(TAG, "client gone after %" PRIu32 " frames", frames);
    log_dma_heap("after teardown");
#if CONFIG_NODE_STREAM_STOPS_WIFI
    esp_wifi_start();
#endif
}

static void stream_server_task(void *arg)
{
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(listener >= 0);
    int yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(CONFIG_NODE_STREAM_PORT);
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(listener, 1) != 0)
    {
        ESP_LOGE(TAG, "cannot listen on %d", CONFIG_NODE_STREAM_PORT);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "H.264 stream on tcp port %d", CONFIG_NODE_STREAM_PORT);

    while (true)
    {
        int client = accept(listener, NULL, NULL);
        if (client < 0)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        ESP_LOGI(TAG, "stream client connected");
        serve_client(client);
        close(client);
    }
}

// ---------------------------------------------------------------------------

extern "C" esp_err_t camera_stream_start(void)
{
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = CAM_I2C_PORT;
    bus_cfg.sda_io_num = (gpio_num_t)CAM_I2C_SDA;
    bus_cfg.scl_io_num = (gpio_num_t)CAM_I2C_SCL;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "i2c bus create failed: %d", err);
        return err;
    }

    err = sgm38121_power_up(bus);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "camera PMIC power-up failed, camera disabled");
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(20)); /* rails up before the sensor is probed */

    esp_ldo_channel_config_t ldo_cfg = {};
    ldo_cfg.chan_id = MIPI_LDO_CHANNEL;
    ldo_cfg.voltage_mv = MIPI_LDO_MV;
    esp_ldo_channel_handle_t ldo = NULL;
    err = esp_ldo_acquire_channel(&ldo_cfg, &ldo);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "MIPI LDO acquire failed: %d", err);
        return err;
    }

    /* Sensor SCCB rides the PMIC's I2C bus (init_sccb=false, shared handle).
     * Kept in statics: every client session re-runs esp_video_init with it. */
    s_csi_config.sccb_config.init_sccb = false;
    s_csi_config.sccb_config.i2c_handle = bus;
    s_csi_config.sccb_config.freq = 100000;
    s_csi_config.reset_pin = -1;
    s_csi_config.pwdn_pin = -1;
    s_video_config.csi = &s_csi_config;

    log_dma_heap("before esp_video_init");

    /* Left initialised for the life of the boot: esp_video_deinit() called
     * straight after a successful init wedges the board (observed: sensor
     * detected, then no further output, then a reset). Only the frame
     * buffers are per-session, and those are what actually cost. */
    err = esp_video_init(&s_video_config);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "no camera sensor found (%d), streaming disabled", err);
        return err;
    }
    s_sensor_present = true;
    log_dma_heap("idle (sensor found, no buffers)");

    if (xTaskCreatePinnedToCore(stream_server_task, "h264_stream", 8192, NULL, 5, NULL, 0) != pdPASS)
    {
        return ESP_FAIL;
    }
    return ESP_OK;
}
