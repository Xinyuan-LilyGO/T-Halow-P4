/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2025-09-19 14:20:25
 * @LastEditTime: 2025-10-13 11:43:37
 * @License: GPL 3.0
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "t_halow_p4_config.h"
#include "cpp_bus_driver_library.h"

#ifdef __cplusplus
extern "C"
{
#endif

#include "hgic_sdspi.h"
#include "hgic_raw.h"

#include "esp_log.h"
#include "esp_wifi_remote.h"
#include "nvs_flash.h"

#ifdef __cplusplus
}
#endif

// std
#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>


#define WIFI_SSID "xinyuandianzi"
#define WIFI_PASS "AA15994823428"

// #define HALOW_DEFAULT_ROLE_AP
// #define HALOW_DEFAULT_ROLE_STA

#define SOFTWARE_SPI_FREQ_HZ 1000000
#define SOFTWARE_SPI_DELAY_US 1000000 / SOFTWARE_SPI_FREQ_HZ

#define MAX_SPI_RECEIVE_SIZE std::min(HGIC_RAW_DATA_ROOM, HGIC_RAW_MAX_PAYLOAD)

#define UART_MONITOR_BUF_SIZE 1024

auto spidrv_read_buffer = std::make_unique<uint8_t[]>(MAX_SPI_RECEIVE_SIZE);

size_t Cycle_Time = 0;

volatile bool Interrupt_Flag = false;

auto Tx_Ah_R900pnr_Spi_Bus = std::make_shared<Cpp_Bus_Driver::Hardware_Spi>(TX_AH_R900PNR_MOSI, TX_AH_R900PNR_SCLK, TX_AH_R900PNR_MISO,
                                                                            SPI2_HOST, 0, spi_clock_source_t ::SPI_CLK_SRC_SPLL);

auto Uart_Bus_halow = std::make_shared<Cpp_Bus_Driver::Hardware_Uart>(TX_AH_R900PNR_DEBUG_UART_TX, TX_AH_R900PNR_DEBUG_UART_RX, UART_NUM_1);

auto Uart_Bus_monitor = std::make_shared<Cpp_Bus_Driver::Hardware_Uart>(T_HALOW_P4_TX, T_HALOW_P4_RX, UART_NUM_0);

auto IIC_Bus_1 = std::make_shared<Cpp_Bus_Driver::Hardware_Iic_1>(SGM38121_SDA, SGM38121_SCL, I2C_NUM_1);
auto SGM38121 = std::make_unique<Cpp_Bus_Driver::Sgm38121>(IIC_Bus_1, SGM38121_IIC_ADDRESS, DEFAULT_CPP_BUS_DRIVER_VALUE);


volatile bool WIFI_init_flag = false;
volatile bool Halow_init_flag = false;
volatile bool SGM38121_init_flag = false;
uint16_t halow_uart_len = 0;

// WIFI
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI("Wi-Fi Event", "获得 IP: " IPSTR, IP2STR(&event->ip_info.ip));

        wifi_scan_config_t scan_config = {.ssid = NULL,         // NULL 表示扫描所有 SSID
                                          .bssid = NULL,        // NULL 表示扫描所有 AP
                                          .channel = 0,         // 0 表示扫描所有信道
                                          .show_hidden = false, // 避免显示隐藏 SSID
                                          .scan_type = WIFI_SCAN_TYPE_ACTIVE};

        ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, false));
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI("Wi-Fi Event", "WiFi 断开，尝试重新连接...");
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE)
    {
        ESP_LOGI("Wi-Fi Event", "热点列表：");
        uint16_t ap_count = 0;
        std::vector<wifi_ap_record_t> ap_info;

        // 获取扫描结果数量
        if (esp_wifi_scan_get_ap_num(&ap_count) != ESP_OK)
        {
            ESP_LOGE("Wi-Fi Event", "获取 AP 数量失败");
            return;
        }

        // 限制最大获取数量，避免内存不足
        if (ap_count > 64)
        {
            ap_count = 64;
        }
        ap_info.resize(ap_count);

        // 获取 Wi-Fi 热点信息
        if (esp_wifi_scan_get_ap_records(&ap_count, ap_info.data()) != ESP_OK)
        {
            ESP_LOGE("Wi-Fi Event", "获取 AP 记录失败");
            return;
        }

        // 释放缓存
        ESP_ERROR_CHECK(esp_wifi_clear_ap_list());

        ESP_LOGI("Wi-Fi Event", "扫描到 %d 个 Wi-Fi 热点：", ap_count);

        // **按信号强度 RSSI 降序排序**
        std::sort(ap_info.begin(), ap_info.end(),
                  [](const wifi_ap_record_t& a, const wifi_ap_record_t& b)
                  {
                      return a.rssi > b.rssi; // RSSI 越大信号越强
                  });

        // **去重 SSID**
        std::unordered_set<std::string> seen_ssids;
        std::vector<wifi_ap_record_t> unique_ap_info;

        for (const auto& info : ap_info)
        {
            std::string ssid_str(reinterpret_cast<const char*>(info.ssid)); // 转换为 std::string
            if (!ssid_str.empty() && seen_ssids.find(ssid_str) == seen_ssids.end())
            {
                seen_ssids.emplace(ssid_str);
                unique_ap_info.emplace_back(info);
            }
        }

        // **遍历并打印 Wi-Fi 热点信息**

        ESP_LOGI("Wi-Fi Event", "-------------------------------------------------------------");
        ESP_LOGI("Wi-Fi Event", "| %-32s | %4s | %7s | %17s |", "SSID", "RSSI", "Channel", "MAC Address");
        ESP_LOGI("Wi-Fi Event", "-------------------------------------------------------------");

        for (const auto& info : unique_ap_info)
        {
            const char* band = (info.primary <= 14) ? "2.4GHz" : "5GHz";
            ESP_LOGI("Wi-Fi Event", "| %-32s | %4d dBm | %3d (%s) | %02X:%02X:%02X:%02X:%02X:%02X |", info.ssid,
                     info.rssi, info.primary, band, info.bssid[0], info.bssid[1], info.bssid[2], info.bssid[3],
                     info.bssid[4], info.bssid[5]);
        }

        ESP_LOGI("Wi-Fi Event", "-------------------------------------------------------------");

        WIFI_init_flag = true;
    }
    else
    {
        ESP_LOGI("Wi-Fi Event", "event %s %ld", event_base, event_id);
    }
}

void wifi_init_sta()
{
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta =
            {
                .ssid = WIFI_SSID,
                .password = WIFI_PASS,
            },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// 单向接口测速，数据从主控发送到模组，模组不做检查
int hgic_raw_test(unsigned char *data, unsigned int len)
{
    struct hgic_frm_hdr2 *hdr = (struct hgic_frm_hdr2 *)hgic.tx_buf;

    if ((len > HGIC_RAW_DATA_ROOM) || (len > HGIC_RAW_MAX_PAYLOAD))
    {
        printf("**too big data**\r\n");
        return -1;
    }

    hdr->hdr.magic = HGIC_HDR_TX_MAGIC;
    hdr->hdr.type = HGIC_HDR_TYPE_TEST;
    hdr->hdr.length = len;
    hdr->hdr.ifidx = 1;
    hdr->hdr.cookie = 0;
    memcpy((unsigned char *)(hdr + 1), data, len);
    return spi_raw_send(hgic.tx_buf, len + sizeof(struct hgic_frm_hdr2));
}

// 双向接口测速，数据从主控发送到模组，模组将数据原样发回
int hgic_raw_test2(unsigned char *data, unsigned int len)
{
    struct hgic_frm_hdr2 *hdr = (struct hgic_frm_hdr2 *)hgic.tx_buf;

    if ((len > HGIC_RAW_DATA_ROOM) || (len > HGIC_RAW_MAX_PAYLOAD))
    {
        printf("**too big data**\r\n");
        return -1;
    }

    hdr->hdr.magic = HGIC_HDR_TX_MAGIC;
    hdr->hdr.type = HGIC_HDR_TYPE_TEST2;
    hdr->hdr.length = len + sizeof(struct hgic_frm_hdr2);
    hdr->hdr.ifidx = 1;
    hdr->hdr.cookie = 0;
    memcpy((unsigned char *)(hdr + 1), data, len);
    return spi_raw_send(hgic.tx_buf, len + sizeof(struct hgic_frm_hdr2));
}

void spidrv_write_read(void *priv, unsigned char *wdata, unsigned char *rdata, unsigned int len)
{
    Tx_Ah_R900pnr_Spi_Bus->write_read(wdata, rdata, len);
}

void spidrv_write(void *priv, unsigned char *data, unsigned int len, char dma_flag)
{
    Tx_Ah_R900pnr_Spi_Bus->write(data, len);
}

void spidrv_read(void *priv, unsigned char *data, unsigned int len, char dma_flag)
{
    Tx_Ah_R900pnr_Spi_Bus->write_read(spidrv_read_buffer.get(), data, len);
}

void spidrv_cs(void *priv, char enable)
{
    // if (enable)
    // {
    //     Tx_Ah_R900pnr_Spi_Bus->pin_write(TX_AH_R900PNR_CS, 0);
    // }
    // else
    // {
    //     Tx_Ah_R900pnr_Spi_Bus->pin_write(TX_AH_R900PNR_CS, 1);
    // }
}

int spidrv_hw_crc(void *priv, unsigned char *data, unsigned int len, char flag)
{
    return 0;
}

int spi_raw_send(unsigned char *data, unsigned int len)
{
    return hgic_sdspi_write(0, data, len);
}

static void echo_task(void *arg)
{
    uart_config_t uart_config = {
        .baud_rate = ECHO_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    int intr_alloc_flags = 0;

#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif

    ESP_ERROR_CHECK(uart_driver_install(ECHO_UART_PORT_NUM, UART_MONITOR_BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(ECHO_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(ECHO_UART_PORT_NUM, ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_RTS, ECHO_TEST_CTS));

    // --------------------------- halow ------------------------------------------------
    uart_config_t halwo_uart_config = {
        .baud_rate = HALOW_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    int halow_intr_alloc_flags = 0;

#if CONFIG_UART_ISR_IN_IRAM
    halow_intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif

    ESP_ERROR_CHECK(uart_driver_install(HALOW_UART_PORT_NUM, UART_MONITOR_BUF_SIZE * 2, 0, 0, NULL, halow_intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(HALOW_UART_PORT_NUM, &halwo_uart_config));
    ESP_ERROR_CHECK(uart_set_pin(HALOW_UART_PORT_NUM, HALOW_TEST_TXD, HALOW_TEST_RXD, HALOW_TEST_RTS, HALOW_TEST_CTS));

    // Configure a temporary buffer for the incoming data
    uint8_t *data = (uint8_t *)malloc(UART_MONITOR_BUF_SIZE);

    while (1)
    {
        // Read data from the UART
        int len = uart_read_bytes(ECHO_UART_PORT_NUM, data, (UART_MONITOR_BUF_SIZE - 1), 20 / portTICK_PERIOD_MS);
        if(len > 0) 
        {   // Write data back to the UART
            uart_write_bytes(HALOW_UART_PORT_NUM, (const char *)data, len);
        }
            
        int len1 = uart_read_bytes(HALOW_UART_PORT_NUM, data, (UART_MONITOR_BUF_SIZE - 1), 20 / portTICK_PERIOD_MS);
        if(len1 > 0) 
        {   // Write data back to the UART
            halow_uart_len += len1;
            uart_write_bytes(ECHO_UART_PORT_NUM, (const char *)data, len1);
        }
            
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

extern "C" void app_main(void)
{
    printf("Ciallo\n");

    SGM38121_init_flag = SGM38121->begin();

    SGM38121->set_output_voltage(Cpp_Bus_Driver::Sgm38121::Channel::DVDD_1, 1500);
    SGM38121->set_output_voltage(Cpp_Bus_Driver::Sgm38121::Channel::AVDD_1, 1700);
    SGM38121->set_output_voltage(Cpp_Bus_Driver::Sgm38121::Channel::AVDD_2, 3000);
    SGM38121->set_channel_status(Cpp_Bus_Driver::Sgm38121::Channel::DVDD_1, Cpp_Bus_Driver::Sgm38121::Status::ON);
    SGM38121->set_channel_status(Cpp_Bus_Driver::Sgm38121::Channel::AVDD_1, Cpp_Bus_Driver::Sgm38121::Status::ON);
    SGM38121->set_channel_status(Cpp_Bus_Driver::Sgm38121::Channel::AVDD_2, Cpp_Bus_Driver::Sgm38121::Status::ON);

    Tx_Ah_R900pnr_Spi_Bus->create_gpio_interrupt(TX_AH_R900PNR_INT, Cpp_Bus_Driver::Tool::Interrupt_Mode::FALLING,
                                                 [](void *arg) -> IRAM_ATTR void
                                                 {
                                                     Interrupt_Flag = true;
                                                 });

                                    

    // Uart_Bus_halow->begin(115200);
    // Uart_Bus_monitor->begin(115200);

    memset(spidrv_read_buffer.get(), 0xFF, MAX_SPI_RECEIVE_SIZE);
    // tx-ah模块通信频率必须为40mhz
    Tx_Ah_R900pnr_Spi_Bus->begin(40000000, TX_AH_R900PNR_CS);

    // 等待sdio从设备初始化
    vTaskDelay(pdMS_TO_TICKS(1000));

    int32_t assert = hgic_sdspi_init(0);
    while (assert == -1)
    {
        printf("hgic_sdspi_init fail (error code: %ld)\n", assert);
        assert = hgic_sdspi_init(0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    printf("hgic_sdspi_init success\n");

    if (hgic_raw_get_fwinfo() == -1)
    {
        printf("hgic_raw_set_mode fail\n");
    }
    else
    {
        printf("hgic_raw_get_fwinfo success\n");
        Halow_init_flag = true;
    }

#ifdef HALOW_DEFAULT_ROLE_AP
    hgic_raw_set_mode((char *)"ap");
    hgic_raw_set_sysdbg("lmac,0");
#elif HALOW_DEFAULT_ROLE_STA
    hgic_raw_set_mode((char *)"sta");
#endif

    vTaskDelay(pdMS_TO_TICKS(100));

    wifi_init_sta();

    // halow debug info
    xTaskCreate(echo_task, "uart_echo_task", 3072, NULL, 10, NULL);

    while (1)
    {
        if (hgic_sdspi_detect_alive(0) == -1)
        {
            printf("hgic_sdspi_detect_alive fail hgic_sdspi_init start\n");
            hgic_sdspi_init(0);
            hgic_raw_set_sysdbg("lmac,0");
        }

        if (Interrupt_Flag == true)
        {
            auto buffer = std::make_unique<unsigned char[]>(1024);

            size_t length = hgic_sdspi_read(0, buffer.get(), 1024, 0);

            if (length != static_cast<size_t>(-1) && length > 0)
            {
                unsigned char *buf_p = buffer.get();
                unsigned int length_2 = static_cast<unsigned int>(length);
                static int recv_cnt = 0;
                
                if (hgic_raw_rx(&buf_p, &length_2) == HGIC_RAW_RX_TYPE_DATA)
                {
                    // printf("spi receive length: %d\n", length);
                    printf(" ------------------- [%d] receive %d data ------------------- \n", recv_cnt++, length);

                    printf("Destination address: %x-%x-%x-%x-%x-%x\n", buf_p[0], buf_p[1], buf_p[2],
                                                                       buf_p[3], buf_p[4], buf_p[5]);
                    printf("Device address     : %x-%x-%x-%x-%x-%x\n", buf_p[6], buf_p[7], buf_p[8],
                                                                       buf_p[9], buf_p[10], buf_p[11]);
                    printf("ETH type           : %x-%x\n", buf_p[12], buf_p[13]);

                    printf("Date: ");
                    for (uint32_t i = 14; i < length_2; i++)
                    {
                        printf("%c", buf_p[i]);
                    }

                    printf("\n");
                }
            }
            Interrupt_Flag = false;
        }

        if (Tx_Ah_R900pnr_Spi_Bus->get_system_time_ms() > Cycle_Time)
        {
            // printf("test data send start\n");

            // if (hgic_raw_test2((unsigned char *)"0123456789", 10) == -1)
            // {
            //     printf("hgic_raw_test2 fail\n");
            // }
#ifdef HALOW_DEFAULT_ROLE_STA 
            uint8_t dest[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
            static int count = 10000;
            std::string str = "T-Halow-P4" + std::to_string(count++);
            printf("test data send: %s\n", str.c_str());
            if(hgic_raw_send(dest, (unsigned char *)str.c_str(), str.length()) == -1)
            {
                printf("hgic_raw_send fail\n");
            }
            Cycle_Time = Tx_Ah_R900pnr_Spi_Bus->get_system_time_ms() + 1000;
#elif HALOW_DEFAULT_ROLE_AP
            uint8_t dest[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
            static int count = 10000;
            std::string str = "AP " + std::to_string(count++);
            printf("test data send: %s\n", str.c_str());
            if(hgic_raw_send(dest, (unsigned char *)str.c_str(), str.length()) == -1)
            {
                printf("hgic_raw_send fail\n");
            }
            Cycle_Time = Tx_Ah_R900pnr_Spi_Bus->get_system_time_ms() + 3000;
#endif

            if(WIFI_init_flag) {
                printf("\n\n");
                printf("Halow .................. %s\n", (halow_uart_len > 0) ? "成功" : "失败");
                printf("WIFI (esp32c6) ......... %s\n", (WIFI_init_flag == true) ? "成功" : "失败");
                printf("SGM38121 ............... %s\n", (SGM38121_init_flag == true) ? "成功" : "失败");
                printf("halow_uart_len ......... %d\n", halow_uart_len);
                printf("\n");
            }
            Cycle_Time = Tx_Ah_R900pnr_Spi_Bus->get_system_time_ms() + 5000;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
        
    }
}
