/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2025-09-19 14:20:25
 * @LastEditTime: 2025-10-13 15:50:02
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
extern "C"
{
#include "hgic_sdspi.h"
#include "hgic_raw.h"
}

// #define MAX_SPI_SEND_SIZE 1024
#define MAX_SPI_SEND_SIZE std::min(HGIC_RAW_DATA_ROOM, HGIC_RAW_MAX_PAYLOAD) - 400

#define MAX_SPI_RECEIVE_SIZE std::min(HGIC_RAW_DATA_ROOM, HGIC_RAW_MAX_PAYLOAD)

auto tx_buffer = std::make_unique<uint8_t[]>(MAX_SPI_RECEIVE_SIZE);
auto rx_buffer = std::make_unique<uint8_t[]>(MAX_SPI_RECEIVE_SIZE);

auto spidrv_read_buffer = std::make_unique<uint8_t[]>(MAX_SPI_RECEIVE_SIZE);

size_t total_transmit_bytes = 0;
size_t total_failed_received_bytes = 0;

double last_time_us = 0;
double spi_time_elapsed_ms = 0;

size_t Cycle_Time = 0;
size_t Cycle_Time_2 = 0;

volatile bool Interrupt_Flag = false;

auto Tx_Ah_R900pnr_Spi_Bus = std::make_shared<Cpp_Bus_Driver::Hardware_Spi>(TX_AH_R900PNR_MOSI, TX_AH_R900PNR_SCLK, TX_AH_R900PNR_MISO,
                                                                            SPI2_HOST, 0, spi_clock_source_t ::SPI_CLK_SRC_SPLL);

auto Uart_Bus_1 = std::make_shared<Cpp_Bus_Driver::Hardware_Uart>(TX_AH_R900PNR_DEBUG_UART_RX, TX_AH_R900PNR_DEBUG_UART_TX, UART_NUM_1);

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

void Tx_Ah_Send_Task(void *arg)
{
    printf("Tx_Ah_Send_Task start\n");

    while (1)
    {
        // if (Tx_Ah_R900pnr_Spi_Bus->get_system_time_ms() > Cycle_Time)
        // {
        // printf("test data send start\n");

        double current_time = Tx_Ah_R900pnr_Spi_Bus->get_system_time_us();
        if (hgic_raw_test2(tx_buffer.get(), MAX_SPI_SEND_SIZE) == -1)
        {
            printf("hgic_raw_test2 fail\n");
        }
        double current_time_2 = Tx_Ah_R900pnr_Spi_Bus->get_system_time_us();

        spi_time_elapsed_ms += (current_time_2 - current_time) / 1000.0;

        //     Cycle_Time = Tx_Ah_R900pnr_Spi_Bus->get_system_time_ms() + 10;
        // }

        // vTaskDelay(pdMS_TO_TICKS(10));
        usleep(10000);
    }
}

void Tx_Ah_Receive_Task(void *arg)
{
    printf("Tx_Ah_Receive_Task start\n");

    while (1)
    {
        if (Interrupt_Flag == true)
        {
            // printf("Interrupt_Flag == true\n");

            double current_time = Tx_Ah_R900pnr_Spi_Bus->get_system_time_us();
            size_t length = hgic_sdspi_read(0, rx_buffer.get(), MAX_SPI_RECEIVE_SIZE, 0);
            double current_time_2 = Tx_Ah_R900pnr_Spi_Bus->get_system_time_us();

            spi_time_elapsed_ms += (current_time_2 - current_time) / 1000.0;

            // printf("spi receive length: %d\n", length);

            if (length <= 0)
            {
                printf("hgic_sdspi_read fail (length: %d)\n", length);
                total_failed_received_bytes += length;
            }
            else
            {
                unsigned char *buffer_p = rx_buffer.get();
                unsigned int length_2 = static_cast<unsigned int>(length);

                if (hgic_raw_rx(&buffer_p, &length_2) == HGIC_RAW_RX_TYPE_DATA)
                {
                    // if (Tx_Ah_R900pnr_Spi_Bus->search(tx_buffer.get(), length_2, (const char *)buffer_p, length_2) == true)
                    // {
                    // printf("length_2: %d\n", length_2);
                    total_transmit_bytes += length_2;
                    // }
                    // else
                    // {
                    //     printf("search fail\n");
                    //     total_failed_received_bytes += length_2;
                    // }
                }
                else
                {
                    // printf("hgic_raw_rx fail (length_2: %d)\n", length_2);
                    total_failed_received_bytes += length_2;
                }
            }

            Interrupt_Flag = false;
        }

        // vTaskDelay(pdMS_TO_TICKS(10));
        usleep(10000);
    }
}

extern "C" void app_main(void)
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

    Uart_Bus_1->begin(115200);

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

    // if (hgic_raw_get_fwinfo() == -1)
    // {
    //     printf("hgic_raw_set_mode fail\n");
    // }
    // else
    // {
    //     printf("hgic_raw_get_fwinfo success\n");
    // }

    // if (hgic_raw_set_mode((char *)"ap") == -1)
    // {
    //     printf("hgic_raw_set_mode fail\n");
    // }
    // else
    // {
    //     printf("hgic_raw_set_mode success\n");
    // }

    // uart_flush_input(UART_NUM_0);

    memset(tx_buffer.get(), '9', MAX_SPI_RECEIVE_SIZE);

    xTaskCreate(Tx_Ah_Receive_Task, "Tx_Ah_Receive_Task", 4 * 1024, NULL, 2, NULL);
    xTaskCreate(Tx_Ah_Send_Task, "Tx_Ah_Send_Task", 4 * 1024, NULL, 2, NULL);

    while (1)
    {
        // 检测设备是否存活
        if (hgic_sdspi_detect_alive(0) == -1)
        {
            printf("hgic_sdspi_detect_alive fail hgic_sdspi_init start\n");
            hgic_sdspi_init(0);
        }

        if (Tx_Ah_R900pnr_Spi_Bus->get_system_time_ms() > Cycle_Time_2)
        {
            double current_time = Tx_Ah_R900pnr_Spi_Bus->get_system_time_us();

            // 双向数据传输需要*2
            total_transmit_bytes *= 2;
            double total_time_elapsed_s = (current_time - last_time_us) / 1000.0 / 1000.0;
            double spi_time_elapsed_s = spi_time_elapsed_ms / 1000.0;
            double total_speed_bps = total_transmit_bytes * 8 / total_time_elapsed_s;
            double total_speed_bytes = total_transmit_bytes / total_time_elapsed_s;
            double spi_speed_bps = total_transmit_bytes * 8 / spi_time_elapsed_s;
            double spi_speed_bytes = total_transmit_bytes / spi_time_elapsed_s;

            printf("\n-------------------- transmit status --------------------\n");
            printf("total time elapsed: %.6f s\n", total_time_elapsed_s);
            printf("spi time elapsed: %.6f s\n", spi_time_elapsed_s);
            printf("total transmit data size: %.6f mbytes\n", total_transmit_bytes / (1024.0 * 1024.0));
            printf("total speed: %.6f mbytes/s, %.6f mbps\n", total_speed_bytes / (1024.0 * 1024.0), total_speed_bps / (1000.0 * 1000.0));
            printf("spi speed: %.6f mbytes/s, %.6f mbps\n", spi_speed_bytes / (1024.0 * 1024.0), spi_speed_bps / (1000.0 * 1000.0));
            printf("failed packets: %d\n", total_failed_received_bytes);
            printf("----------------------------------------------------------------------\n");

            Cycle_Time_2 = Tx_Ah_R900pnr_Spi_Bus->get_system_time_ms() + 5000;
            total_transmit_bytes = 0;
            total_failed_received_bytes = 0;
            spi_time_elapsed_ms = 0;
            last_time_us = current_time;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
