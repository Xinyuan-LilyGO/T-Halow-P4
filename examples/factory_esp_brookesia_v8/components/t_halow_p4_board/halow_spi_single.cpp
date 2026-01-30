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
#include "t_halow_p4_board.h"
#include "cpp_bus_driver_library.h"
extern "C"
{
#include "hgic_sdspi.h"
#include "hgic_raw.h"
}
#include "lvgl.h"

#include "../esp_brookesia_app/wifi_halow/ui/ui.h"

#define HALOW_DEFAULT_ROLE_AP
// #define HALOW_DEFAULT_ROLE_STA

#define SOFTWARE_SPI_FREQ_HZ 1000000
#define SOFTWARE_SPI_DELAY_US 1000000 / SOFTWARE_SPI_FREQ_HZ

#define MAX_SPI_RECEIVE_SIZE std::min(HGIC_RAW_DATA_ROOM, HGIC_RAW_MAX_PAYLOAD)

#define UART_MONITOR_BUF_SIZE 4096
#define UART_LINE_MAX 128


TaskHandle_t halow_echo_task_handle = NULL;


static QueueHandle_t uart_queue;
 QueueHandle_t lvgl_msg_queue;

 RingbufHandle_t log_rb;


auto spidrv_read_buffer = std::make_unique<uint8_t[]>(MAX_SPI_RECEIVE_SIZE);

size_t Cycle_Time = 0;

volatile bool Interrupt_Flag = false;

auto Tx_Ah_R900pnr_Spi_Bus = std::make_shared<Cpp_Bus_Driver::Hardware_Spi>(TX_AH_R900PNR_MOSI, TX_AH_R900PNR_SCLK, TX_AH_R900PNR_MISO,
                                                                            SPI2_HOST, 0, spi_clock_source_t ::SPI_CLK_SRC_SPLL);

auto Uart_Bus_halow = std::make_shared<Cpp_Bus_Driver::Hardware_Uart>(TX_AH_R900PNR_DEBUG_UART_TX, TX_AH_R900PNR_DEBUG_UART_RX, UART_NUM_1);

auto Uart_Bus_monitor = std::make_shared<Cpp_Bus_Driver::Hardware_Uart>(T_HALOW_P4_TX, T_HALOW_P4_RX, UART_NUM_0);


// static BaseType_t halow_echo_task_handle = NULL;

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
    return raw_send(hgic.tx_buf, len + sizeof(struct hgic_frm_hdr2));
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
    return raw_send(hgic.tx_buf, len + sizeof(struct hgic_frm_hdr2));
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

int raw_send(unsigned char *data, unsigned int len)
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

    ESP_ERROR_CHECK(uart_driver_install(ECHO_UART_PORT_NUM, UART_MONITOR_BUF_SIZE, 0, 0, NULL, intr_alloc_flags));
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

    ESP_ERROR_CHECK(uart_driver_install(HALOW_UART_PORT_NUM, UART_MONITOR_BUF_SIZE, 4096, 10, &uart_queue, halow_intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(HALOW_UART_PORT_NUM, &halwo_uart_config));
    ESP_ERROR_CHECK(uart_set_pin(HALOW_UART_PORT_NUM, HALOW_TEST_TXD, HALOW_TEST_RXD, HALOW_TEST_RTS, HALOW_TEST_CTS));

    lvgl_msg_queue = xQueueCreate(8, UART_LINE_MAX);
    log_rb = xRingbufferCreate(8 * 1024, RINGBUF_TYPE_BYTEBUF);
    // Configure a temporary buffer for the incoming data
    uint8_t* uart_data_buf = (uint8_t *)malloc(UART_MONITOR_BUF_SIZE);
    uart_event_t event;

    while (1)
    {
        // Read data from the UART
        int len = uart_read_bytes(ECHO_UART_PORT_NUM, uart_data_buf, (UART_MONITOR_BUF_SIZE - 1), 20 / portTICK_PERIOD_MS);
        if(len > 0) 
        {   // Write data back to the UART
            uart_write_bytes(HALOW_UART_PORT_NUM, (const char *)uart_data_buf, len);
        }

        // int len1 = uart_read_bytes(HALOW_UART_PORT_NUM, data, (UART_MONITOR_BUF_SIZE - 1), 20 / portTICK_PERIOD_MS);
        // if(len1 > 0) 
        // {   // Write data back to the UART
        //     uart_write_bytes(ECHO_UART_PORT_NUM, (const char *)data, len1);
        // }

        int len1 = uart_read_bytes(HALOW_UART_PORT_NUM, uart_data_buf, (UART_MONITOR_BUF_SIZE - 1), 20 / portTICK_PERIOD_MS);
        if (len1 > 0) {
            uart_write_bytes(ECHO_UART_PORT_NUM, (const char *)uart_data_buf, len1);
            // if(len1 < 256)
                xRingbufferSend(log_rb, uart_data_buf, len1, 0);
        }
            
        // if (xQueueReceive(uart_queue, &event, portMAX_DELAY)) {
        //     if (event.type == UART_DATA) {
        //         int len = uart_read_bytes(HALOW_UART_PORT_NUM, data, event.size, pdMS_TO_TICKS(100) );

        //         if (len > 0) {
                    
        //             // data[len] = '\0';
        //             uart_write_bytes(ECHO_UART_PORT_NUM, (const char *)data, len);
        //             // 发送给 LVGL 线程
        //             xQueueSend(lvgl_msg_queue, data, portMAX_DELAY);
        //         }
        //     }
        // }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void halow_detect_alive_tack()
{
    // while (1)
    // {
        if (hgic_sdspi_detect_alive(0) == -1)
        {
            printf("hgic_sdspi_detect_alive fail hgic_sdspi_init start\n");
            hgic_sdspi_init(0);
        }
        if(Interrupt_Flag == true)
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
#else 
            uint8_t dest[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
            static int count = 10000;
            std::string str = "AP " + std::to_string(count++);
            printf("test data send: %s\n", str.c_str());
            if(hgic_raw_send(dest, (unsigned char *)str.c_str(), str.length()) == -1)
            {
                printf("hgic_raw_send fail\n");
            }
            Cycle_Time = Tx_Ah_R900pnr_Spi_Bus->get_system_time_ms() + 1000;
#endif
        }
    //     vTaskDelay(pdMS_TO_TICKS(10));
    // }
    
}


void halow_init(void)
{
    Tx_Ah_R900pnr_Spi_Bus->create_gpio_interrupt(TX_AH_R900PNR_INT, Cpp_Bus_Driver::Tool::Interrupt_Mode::FALLING,
                                                 [](void *arg) -> IRAM_ATTR void
                                                 {
                                                     Interrupt_Flag = true;
                                                 });
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
    }

    hgic_raw_set_sysdbg("lmac,0");

    
    vTaskDelay(pdMS_TO_TICKS(1000));
    xTaskCreate(echo_task, "uart_echo_task", 3072, NULL, 10, &halow_echo_task_handle);
    // xTaskCreate(halow_detect_alive_tack, "halow_detect_alive_tack", 4096, NULL, 10, NULL);

    halow_echo_suspend();
}

void halow_echo_suspend(void)
{
    vTaskSuspend(halow_echo_task_handle);
}

void halow_echo_resume(void)
{
    vTaskResume(halow_echo_task_handle);
}

void halow_spi_test(void)
{
    printf("Ciallo\n");

    // Tx_Ah_R900pnr_Spi_Bus->pin_mode(TX_AH_R900PNR_CS, Cpp_Bus_Driver::Tool::Pin_Mode::OUTPUT, Cpp_Bus_Driver::Tool::Pin_Status::PULLUP);
    // Tx_Ah_R900pnr_Spi_Bus->pin_mode(TX_AH_R900PNR_MOSI, Cpp_Bus_Driver::Tool::Pin_Mode::OUTPUT);
    // Tx_Ah_R900pnr_Spi_Bus->pin_mode(TX_AH_R900PNR_SCLK, Cpp_Bus_Driver::Tool::Pin_Mode::OUTPUT);
    // Tx_Ah_R900pnr_Spi_Bus->pin_mode(TX_AH_R900PNR_MISO, Cpp_Bus_Driver::Tool::Pin_Mode::INPUT);

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
    }

#ifdef HALOW_DEFAULT_ROLE_AP
    hgic_raw_set_mode((char *)"ap");
    hgic_raw_set_sysdbg("lmac,0");
#else 
    hgic_raw_set_mode((char *)"sta");
#endif

    // uart_flush_input(UART_NUM_0);

    // halow debug info
    xTaskCreate(echo_task, "uart_echo_task", 3072, NULL, 10, &halow_echo_task_handle);

//     while (1)
//     {
//         if (hgic_sdspi_detect_alive(0) == -1)
//         {
//             printf("hgic_sdspi_detect_alive fail hgic_sdspi_init start\n");
//             hgic_sdspi_init(0);
//         }

//         if (Interrupt_Flag == true)
//         {
//             auto buffer = std::make_unique<unsigned char[]>(1024);

//             size_t length = hgic_sdspi_read(0, buffer.get(), 1024, 0);

//             if (length != static_cast<size_t>(-1) && length > 0)
//             {
//                 unsigned char *buf_p = buffer.get();
//                 unsigned int length_2 = static_cast<unsigned int>(length);
//                 static int recv_cnt = 0;
                
//                 if (hgic_raw_rx(&buf_p, &length_2) == HGIC_RAW_RX_TYPE_DATA)
//                 {
//                     // printf("spi receive length: %d\n", length);
//                     printf(" ------------------- [%d] receive %d data ------------------- \n", recv_cnt++, length);

//                     printf("Destination address: %x-%x-%x-%x-%x-%x\n", buf_p[0], buf_p[1], buf_p[2],
//                                                                        buf_p[3], buf_p[4], buf_p[5]);
//                     printf("Device address     : %x-%x-%x-%x-%x-%x\n", buf_p[6], buf_p[7], buf_p[8],
//                                                                        buf_p[9], buf_p[10], buf_p[11]);
//                     printf("ETH type           : %x-%x\n", buf_p[12], buf_p[13]);

//                     printf("Date: ");
//                     for (uint32_t i = 14; i < length_2; i++)
//                     {
//                         printf("%c", buf_p[i]);
//                     }

//                     printf("\n");
//                 }
//             }
//             Interrupt_Flag = false;
//         }

//         if (Tx_Ah_R900pnr_Spi_Bus->get_system_time_ms() > Cycle_Time)
//         {
//             // printf("test data send start\n");

//             // if (hgic_raw_test2((unsigned char *)"0123456789", 10) == -1)
//             // {
//             //     printf("hgic_raw_test2 fail\n");
//             // }
// #ifdef HALOW_DEFAULT_ROLE_STA 
//             uint8_t dest[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
//             static int count = 10000;
//             std::string str = "T-Halow-P4" + std::to_string(count++);
//             printf("test data send: %s\n", str.c_str());
//             if(hgic_raw_send(dest, (unsigned char *)str.c_str(), str.length()) == -1)
//             {
//                 printf("hgic_raw_send fail\n");
//             }
//             Cycle_Time = Tx_Ah_R900pnr_Spi_Bus->get_system_time_ms() + 1000;
// #else 
//             uint8_t dest[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
//             static int count = 10000;
//             std::string str = "AP " + std::to_string(count++);
//             printf("test data send: %s\n", str.c_str());
//             if(hgic_raw_send(dest, (unsigned char *)str.c_str(), str.length()) == -1)
//             {
//                 printf("hgic_raw_send fail\n");
//             }
//             Cycle_Time = Tx_Ah_R900pnr_Spi_Bus->get_system_time_ms() + 3000;
// #endif

//             // if (hgic_raw_get_fwinfo() == -1)
//             // {
//             //     printf("hgic_raw_get_fwinfo fail\n");
//             // }
//         }

//         vTaskDelay(pdMS_TO_TICKS(1));
//     }
}
