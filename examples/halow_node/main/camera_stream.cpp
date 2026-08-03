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
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_ldo_regulator.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "nvs.h"
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
#include "esp_http_server.h"
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
/* ~0.7 s at 22 fps: enough for AE/AGC to settle on a still. */
#define SNAPSHOT_SETTLE_FRAMES 16

static int s_cap_fd = -1;
static int s_m2m_fd = -1;
static uint8_t *s_cap_buf[CAP_BUFFER_COUNT];
static size_t s_cap_buf_len[CAP_BUFFER_COUNT];
static uint8_t *s_enc_buf;
static size_t s_enc_buf_len;
static esp_video_init_csi_config_t s_csi_config;
static esp_video_init_config_t s_video_config;

typedef enum
{
    CAM_ENC_H264,
    CAM_ENC_JPEG,
} cam_encoder_t;

static cam_encoder_t s_encoder = CAM_ENC_H264;
static uint32_t s_width, s_height;
static int32_t s_jpeg_quality = CONFIG_NODE_JPEG_QUALITY;

static bool s_sensor_present = false;
static bool s_streaming = false;

/*
 * Master switch for the H.264 server, persisted so it survives the reboots
 * a field unit does on its own. Off means the pipeline is never built: no
 * encoder, no per-viewer setup/teardown, nothing competing for the radio --
 * which is what you want when the point of the exercise is to measure the
 * link itself rather than to watch through it. The browser preview is
 * deliberately not gated by this: it only runs while someone holds the page
 * open, so it cannot churn away in the background.
 */
static bool s_stream_enabled = true;
static uint32_t s_start_failures = 0;

extern "C" uint32_t camera_stream_failures(void)
{
    return s_start_failures;
}

#define CAM_NVS_NS "router"
#define CAM_NVS_KEY "cam_stream"

extern "C" bool camera_stream_enabled(void)
{
    return s_stream_enabled;
}

extern "C" void camera_stream_set_enabled(bool enable)
{
    s_stream_enabled = enable;

    nvs_handle_t h;
    if (nvs_open(CAM_NVS_NS, NVS_READWRITE, &h) == ESP_OK)
    {
        nvs_set_u8(h, CAM_NVS_KEY, enable ? 1 : 0);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "H.264 stream %s", enable ? "enabled" : "disabled");
}

static void load_enabled_flag(void)
{
    nvs_handle_t h;
    uint8_t v = 1;
    if (nvs_open(CAM_NVS_NS, NVS_READONLY, &h) == ESP_OK)
    {
        nvs_get_u8(h, CAM_NVS_KEY, &v);
        nvs_close(h);
    }
    s_stream_enabled = (v != 0);
}

/* One camera, one pipeline: the H.264 server and the browser preview take
 * turns rather than fighting over the sensor. */
static SemaphoreHandle_t s_session_lock;

/* Live figures for /api/link -- what a phone watches while walking away. */
static volatile uint32_t s_stat_fps_x10, s_stat_kbits, s_stat_frame_bytes;
static volatile bool s_stat_viewer;

extern "C" void camera_stream_stats(bool *viewer, uint32_t *fps_x10, uint32_t *kbits,
                                    uint32_t *frame_bytes)
{
    *viewer = s_stat_viewer;
    *fps_x10 = s_stat_fps_x10;
    *kbits = s_stat_kbits;
    *frame_bytes = s_stat_frame_bytes;
}

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

static esp_err_t set_codec_control(int fd, uint32_t id, int32_t value,
                                   uint32_t ctrl_class = V4L2_CID_CODEC_CLASS)
{
    struct v4l2_ext_controls controls;
    struct v4l2_ext_control control[1];

    memset(&controls, 0, sizeof(controls));
    memset(control, 0, sizeof(control));
    controls.ctrl_class = ctrl_class;
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

static esp_err_t pipeline_setup(cam_encoder_t enc)
{
    struct v4l2_format format;
    struct v4l2_requestbuffers req;
    struct v4l2_buffer buf;
    uint32_t width, height;

    /* The two encoders want different things off the ISP: the H.264 core
     * only accepts YUV420, the JPEG core takes RGB565 (and subsamples to
     * 4:2:2 itself). Everything downstream is identical. */
    const bool jpeg = (enc == CAM_ENC_JPEG);
    const uint32_t cap_pixfmt = jpeg ? V4L2_PIX_FMT_RGB565 : V4L2_PIX_FMT_YUV420;
    const uint32_t out_pixfmt = jpeg ? V4L2_PIX_FMT_JPEG : V4L2_PIX_FMT_H264;
    const char *enc_dev = jpeg ? ESP_VIDEO_JPEG_DEVICE_NAME : ESP_VIDEO_H264_DEVICE_NAME;

    s_encoder = enc;
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

    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = cap_pixfmt;
    if (ioctl(s_cap_fd, VIDIOC_S_FMT, &format) != 0)
    {
        ESP_LOGE(TAG, "set capture format %" PRIu32 "x%" PRIu32 " failed", width, height);
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

    s_m2m_fd = open(enc_dev, O_RDONLY);
    if (s_m2m_fd < 0)
    {
        ESP_LOGE(TAG, "open %s failed", enc_dev);
        return ESP_FAIL;
    }

    if (jpeg)
    {
        set_codec_control(s_m2m_fd, V4L2_CID_JPEG_COMPRESSION_QUALITY, s_jpeg_quality,
                          V4L2_CID_JPEG_CLASS);
    }
    else
    {
        set_codec_control(s_m2m_fd, V4L2_CID_MPEG_VIDEO_H264_I_PERIOD, CONFIG_NODE_H264_I_PERIOD);
        set_codec_control(s_m2m_fd, V4L2_CID_MPEG_VIDEO_BITRATE, CONFIG_NODE_H264_BITRATE);
        set_codec_control(s_m2m_fd, V4L2_CID_MPEG_VIDEO_H264_MIN_QP, CONFIG_NODE_H264_MIN_QP);
        set_codec_control(s_m2m_fd, V4L2_CID_MPEG_VIDEO_H264_MAX_QP, CONFIG_NODE_H264_MAX_QP);
    }

    /* Encoder input side: fed by pointer straight from the capture buffers. */
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = cap_pixfmt;
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
    format.fmt.pix.pixelformat = out_pixfmt;
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

    s_width = width;
    s_height = height;
    ESP_LOGI(TAG, "pipeline ready: %" PRIu32 "x%" PRIu32 " -> %s", width, height,
             jpeg ? "JPEG" : "H.264");
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

    if (xSemaphoreTake(s_session_lock, pdMS_TO_TICKS(3000)) != pdTRUE)
    {
        ESP_LOGW(TAG, "camera busy (browser preview open), dropping client");
        return;
    }

#if CONFIG_NODE_STREAM_STOPS_WIFI
    /* There is not enough internal DMA-capable RAM on this board to run the
     * capture/encode pipeline and the 2.4 GHz side at once: with WiFi up,
     * STREAMON has ~2 kB to work with and fails. Park WiFi for the session
     * and give it back afterwards -- while someone is watching, the node is
     * reachable over HaLow anyway (through the gateway's portal forward). */
    esp_wifi_stop();
    log_dma_heap("wifi parked");
#endif

    if (pipeline_setup(CAM_ENC_H264) != ESP_OK)
    {
        ESP_LOGE(TAG, "pipeline bring-up failed, dropping client");
        pipeline_teardown();
#if CONFIG_NODE_STREAM_STOPS_WIFI
        esp_wifi_start();
#endif
        xSemaphoreGive(s_session_lock);
        return;
    }
    log_dma_heap("with pipeline up");
    s_streaming = true;

    if (!session_start())
    {
        /* Almost always the encoder failing to find a contiguous ~90 kB for
         * its reference frame: building and tearing the pipeline down for
         * each viewer, and alternating with the JPEG preview, fragments the
         * internal DMA heap until the largest free block drops below what it
         * needs -- with plenty still free in total. One retry costs nothing;
         * if it persists, only a fresh heap fixes it, so take one. The node
         * is back in ~25 s, keeps its role and rolls back on a bad image, and
         * an unattended camera that fixes itself beats one that needs a
         * visit. The uptime guard keeps a boot-time fault (which a restart
         * cannot fix) from becoming a restart loop. */
        session_stop();
        pipeline_teardown();

        vTaskDelay(pdMS_TO_TICKS(1000));
        bool recovered = (pipeline_setup(CAM_ENC_H264) == ESP_OK) && session_start();
        if (!recovered)
        {
            session_stop();
            pipeline_teardown();
            log_dma_heap("after failed h264 start");
#if CONFIG_NODE_STREAM_STOPS_WIFI
            esp_wifi_start();
#endif
            xSemaphoreGive(s_session_lock);

            /* This used to restart the node on the theory that only a fresh
             * heap fixes fragmentation. In the field that was the wrong
             * trade: at the edge of range the link drops often, every drop
             * rebuilds the pipeline, and the node rebooted repeatedly in the
             * middle of a range test -- losing the link telemetry, which was
             * the thing actually worth having. Fail the viewer instead and
             * stay up; the failure is visible in /api/status as a
             * heap_dma_largest below the ~90 kB the encoder needs, and
             * POST /api/reboot is one request away when a restart really is
             * what is wanted. */
            s_start_failures++;
            ESP_LOGE(TAG, "h264 start failed (largest DMA block %u, encoder needs ~92 kB); "
                          "node staying up, stream unavailable until reboot",
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                                MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
            return;
        }
        ESP_LOGW(TAG, "h264 start needed a retry");
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
        {
            int64_t dt = esp_timer_get_time() - t0;
            s_stat_viewer = true;
            s_stat_frame_bytes = (uint32_t)len;
            s_stat_fps_x10 = (uint32_t)(frames * 1e7 / dt);
            s_stat_kbits = (uint32_t)(bytes * 8e3 / dt);
        }
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
    s_stat_viewer = false;
    s_stat_fps_x10 = s_stat_kbits = 0;
#if CONFIG_NODE_STREAM_STOPS_WIFI
    esp_wifi_start();
#endif
    xSemaphoreGive(s_session_lock);
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
        if (!s_stream_enabled)
        {
            /* Closing immediately tells the puller straight away rather than
             * leaving it waiting on a stream that will never start. */
            close(client);
            vTaskDelay(pdMS_TO_TICKS(500));
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
    s_session_lock = xSemaphoreCreateMutex();
    load_enabled_flag();
    ESP_LOGI(TAG, "H.264 stream %s at boot", s_stream_enabled ? "enabled" : "disabled");
    log_dma_heap("idle (sensor found, no buffers)");

    if (xTaskCreatePinnedToCore(stream_server_task, "h264_stream", 8192, NULL, 5, NULL, 0) != pdPASS)
    {
        return ESP_FAIL;
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Browser front end: MJPEG preview for focusing the lens, and a field page
// that doubles as the range-walk instrument.
//
// The lens on these modules is focused by hand, so the useful thing is a live
// picture plus a number that peaks when the image is sharp. Encoded JPEG size
// at fixed quality is that number: detail is high-frequency content, and
// high-frequency content is what survives quantisation, so a sharp frame is a
// bigger frame. No image processing needed, and it costs nothing to read.
//
// MJPEG rather than the H.264 stream because a browser can display
// multipart/x-mixed-replace natively in an <img>, and cannot play raw Annex-B
// at all.
// ---------------------------------------------------------------------------

static esp_err_t preview_open(int quality)
{
    if (!s_sensor_present || s_session_lock == NULL)
    {
        return ESP_ERR_NOT_FOUND;
    }
    if (xSemaphoreTake(s_session_lock, pdMS_TO_TICKS(4000)) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT; /* the H.264 stream has the camera */
    }

    s_jpeg_quality = quality;
    if (pipeline_setup(CAM_ENC_JPEG) != ESP_OK || !session_start())
    {
        session_stop();
        pipeline_teardown();
        xSemaphoreGive(s_session_lock);
        return ESP_FAIL;
    }
    s_streaming = true;
    return ESP_OK;
}

static void preview_close(void)
{
    s_streaming = false;
    session_stop();
    pipeline_teardown();
    xSemaphoreGive(s_session_lock);
}

/* ?q=1..100 -- lower quality means smaller frames, which is what makes the
 * preview usable over HaLow as well as over the board's own SoftAP. */
static int query_quality(httpd_req_t *req)
{
    char query[32], value[8];
    int q = CONFIG_NODE_JPEG_QUALITY;

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "q", value, sizeof(value)) == ESP_OK)
    {
        q = atoi(value);
    }
    return (q < 5) ? 5 : (q > 95 ? 95 : q);
}

static esp_err_t snapshot_get_handler(httpd_req_t *req)
{
    esp_err_t err = preview_open(query_quality(req));
    if (err != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            err == ESP_ERR_TIMEOUT ? "camera busy" : "no camera");
        return ESP_FAIL;
    }

    /* Auto-exposure needs frames to converge, and a single grab lands while
     * the image is still dark. Run the pipeline briefly and keep the last
     * frame -- the preview stream gets this for free by running on. */
    size_t len = 0;
    for (int i = 0; i < SNAPSHOT_SETTLE_FRAMES; i++)
    {
        if (!encode_frame(&len))
        {
            break;
        }
        encode_frame_done();
    }

    esp_err_t ret = ESP_FAIL;
    if (encode_frame(&len))
    {
        s_stat_frame_bytes = (uint32_t)len;
        httpd_resp_set_type(req, "image/jpeg");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        ret = httpd_resp_send(req, (const char *)s_enc_buf, len);
        encode_frame_done();
    }
    preview_close();
    return ret;
}

#define MJPEG_BOUNDARY "thalowframe"

static esp_err_t preview_get_handler(httpd_req_t *req)
{
    esp_err_t err = preview_open(query_quality(req));
    if (err != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            err == ESP_ERR_TIMEOUT ? "camera busy" : "no camera");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=" MJPEG_BOUNDARY);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    uint32_t frames = 0;
    int64_t t0 = esp_timer_get_time();
    char head[96];

    while (true)
    {
        size_t len = 0;
        if (!encode_frame(&len))
        {
            break;
        }

        int n = snprintf(head, sizeof(head),
                         "\r\n--" MJPEG_BOUNDARY "\r\nContent-Type: image/jpeg\r\n"
                         "Content-Length: %u\r\n\r\n",
                         (unsigned)len);

        bool ok = httpd_resp_send_chunk(req, head, n) == ESP_OK &&
                  httpd_resp_send_chunk(req, (const char *)s_enc_buf, len) == ESP_OK;
        encode_frame_done();
        if (!ok)
        {
            break; /* browser navigated away */
        }

        frames++;
        int64_t dt = esp_timer_get_time() - t0;
        s_stat_frame_bytes = (uint32_t)len;
        s_stat_fps_x10 = (uint32_t)(frames * 1e7 / dt);
        s_stat_kbits = (uint32_t)((uint64_t)len * frames * 8e3 / dt);
    }

    preview_close();
    s_stat_fps_x10 = s_stat_kbits = 0;
    httpd_resp_send_chunk(req, NULL, 0);
    ESP_LOGI(TAG, "preview closed after %" PRIu32 " frames", frames);
    return ESP_OK;
}

static const char CAMERA_PAGE[] = R"HTML(<!doctype html><html><head>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>T-Halow-P4 camera</title><style>
:root{color-scheme:dark}
body{margin:0;padding:12px;background:#111;color:#eee;font:15px/1.4 system-ui,sans-serif}
h1{font-size:17px;margin:0 0 10px}
img{width:100%;border-radius:8px;background:#000;display:block}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:8px;margin:10px 0}
.card{background:#1c1c1e;border-radius:8px;padding:9px 11px}
.k{font-size:11px;text-transform:uppercase;letter-spacing:.06em;color:#8c8c92}
.v{font-size:22px;font-variant-numeric:tabular-nums;margin-top:2px}
.bar{height:14px;background:#000;border-radius:7px;overflow:hidden;position:relative;margin-top:6px}
.bar>i{position:absolute;inset:0 auto 0 0;background:linear-gradient(90deg,#2b6,#7d4);width:0}
.bar>u{position:absolute;top:0;bottom:0;width:2px;background:#f60}
button{background:#2c2c2e;color:#eee;border:0;border-radius:7px;padding:9px 13px;font-size:14px;margin-right:6px}
.ok{color:#6d6}.bad{color:#f66}.warn{color:#fb4}
</style></head><body>
<h1>T-Halow-P4 &mdash; focus &amp; range</h1>
<img id=v alt="camera preview" style="display:none">
<div id=off class=card style="text-align:center;padding:22px;color:#8c8c92">
  Preview is off &mdash; the camera is free for the H.264 stream going home.<br>
  Turn it on to focus the lens; turn it off again before a range walk.
</div>
<div class=card style="margin-top:10px">
  <div class=k>Focus &mdash; turn the lens for the highest number</div>
  <div class=v><span id=f>-</span> <span style="font-size:13px;color:#8c8c92">kB/frame &nbsp; peak <span id=pk>-</span></span></div>
  <div class=bar><i id=fb></i><u id=pkb></u></div>
</div>
<div class=grid>
  <div class=card><div class=k>HaLow</div><div class="v" id=lnk>-</div></div>
  <div class=card><div class=k>RSSI</div><div class=v><span id=rssi>-</span> <span style=font-size:13px>dBm</span></div></div>
  <div class=card><div class=k>SNR &mdash; watch this one</div><div class=v id=snr>-</div></div>
  <div class=card><div class=k>Ping gateway</div><div class=v><span id=rtt>-</span> <span style=font-size:13px>ms</span></div></div>
  <div class=card><div class=k>Loss</div><div class=v><span id=loss>-</span> <span style=font-size:13px>%</span></div></div>
  <div class=card><div class=k>Radio rate</div><div class=v><span id=rate>-</span> <span style=font-size:13px>kb/s</span></div></div>
  <div class=card><div class=k>Video to house</div><div class=v><span id=vid>-</span></div></div>
</div>
<div class=card style="display:flex;align-items:center;justify-content:space-between;gap:10px">
  <div>
    <div class=k>H.264 stream to the house</div>
    <div style="font-size:12px;color:#8c8c92;margin-top:3px">Turn off for a link-only range test &mdash; no encoder, no reconnect churn.</div>
  </div>
  <button id=sw onclick="setStream()" style="min-width:104px">&hellip;</button>
</div>
<button id=tog onclick="toggle()">start preview</button>
<button onclick="q=Math.max(10,q-15);if(on)start()">lower quality</button>
<button onclick="q=Math.min(90,q+15);if(on)start()">higher quality</button>
<button onclick="pk=0">reset peak</button>
<p style="color:#8c8c92;font-size:13px">The camera does one job at a time: while the preview is on, the H.264 stream to the house is refused, and vice versa. RSSI, ping and loss keep updating either way &mdash; they come over this node's own WiFi, not over HaLow.</p>
<script>
var q=40,pk=0,on=false,se=true;
function setStream(){
 se=!se;
 fetch('/api/camera',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
   body:'enabled='+(se?1:0)}).then(r=>r.json()).then(d=>{se=d.stream_enabled;paint()}).catch(()=>{});
}
function paint(){
 var b=document.getElementById('sw');
 b.textContent=se?'stream ON':'stream OFF';
 b.style.background=se?'#2b6':'#633';
}
function start(){document.getElementById('v').src='/preview.mjpg?q='+q+'&t='+Date.now()}
function toggle(){
 on=!on;
 var v=document.getElementById('v');
 document.getElementById('off').style.display=on?'none':'';
 v.style.display=on?'':'none';
 document.getElementById('tog').textContent=on?'stop preview':'start preview';
 if(on){start()}else{v.src='';}
}
setInterval(function(){
 fetch('/api/link').then(r=>r.json()).then(function(d){
  var kb=d.frame_bytes/1024;
  document.getElementById('f').textContent=kb.toFixed(1);
  if(kb>pk)pk=kb;
  document.getElementById('pk').textContent=pk.toFixed(1);
  var scale=Math.max(pk,1)*1.15;
  document.getElementById('fb').style.width=(100*kb/scale)+'%';
  document.getElementById('pkb').style.left=(100*pk/scale)+'%';
  var l=document.getElementById('lnk');
  l.textContent=d.halow_up?'associated':'DOWN';
  l.className='v '+(d.halow_up?'ok':'bad');
  document.getElementById('rssi').textContent=d.rssi;
  var sn=document.getElementById('snr');
  sn.textContent=d.snr;
  /* walk 1: clean at SNR 18, gone at 17 -- the cliff is that sharp */
  sn.className='v '+(d.snr<20?'bad':(d.snr<28?'warn':'ok'));
  document.getElementById('rate').textContent=d.tx_bitrate;
  var rt=document.getElementById('rtt');
  rt.textContent=d.ping_ms<0?'--':d.ping_ms;
  rt.className=d.ping_ms<0?'bad':(d.ping_ms>250?'warn':'ok');
  var ls=document.getElementById('loss');
  ls.textContent=d.loss_pct;
  ls.className=d.loss_pct>20?'bad':(d.loss_pct>5?'warn':'ok');
  document.getElementById('vid').textContent=d.stream_enabled?(d.viewer?(d.kbits+' kb/s'):'no viewer'):'disabled';
  if(d.stream_enabled!==undefined&&d.stream_enabled!==se){se=d.stream_enabled;}
  paint();
 }).catch(function(){});
},1000);
</script></body></html>)HTML";

static esp_err_t camera_post_handler(httpd_req_t *req)
{
    char body[64] = {0};
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "expected enabled=0|1");
        return ESP_FAIL;
    }

    char value[8] = {0};
    if (httpd_query_key_value(body, "enabled", value, sizeof(value)) != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "expected enabled=0|1");
        return ESP_FAIL;
    }
    camera_stream_set_enabled(atoi(value) != 0);

    char json[64];
    snprintf(json, sizeof(json), "{\"stream_enabled\":%s}",
             camera_stream_enabled() ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t camera_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, CAMERA_PAGE, HTTPD_RESP_USE_STRLEN);
}

extern "C" void camera_web_register(httpd_handle_t server)
{
    static const httpd_uri_t page = {
        .uri = "/camera", .method = HTTP_GET, .handler = camera_page_handler, .user_ctx = NULL};
    static const httpd_uri_t preview = {
        .uri = "/preview.mjpg", .method = HTTP_GET, .handler = preview_get_handler, .user_ctx = NULL};
    static const httpd_uri_t snapshot = {
        .uri = "/snapshot.jpg", .method = HTTP_GET, .handler = snapshot_get_handler, .user_ctx = NULL};

    static const httpd_uri_t api_camera = {
        .uri = "/api/camera", .method = HTTP_POST, .handler = camera_post_handler, .user_ctx = NULL};

    httpd_register_uri_handler(server, &page);
    httpd_register_uri_handler(server, &preview);
    httpd_register_uri_handler(server, &snapshot);
    httpd_register_uri_handler(server, &api_camera);
}
