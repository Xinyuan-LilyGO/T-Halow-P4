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
extern "C"
{
#include "hgic_sdspi.h"
#include "hgic_raw.h"
}

#define SOFTWARE_SPI_FREQ_HZ 1000000
#define SOFTWARE_SPI_DELAY_US 1000000 / SOFTWARE_SPI_FREQ_HZ

#define MAX_SPI_RECEIVE_SIZE std::min(HGIC_RAW_DATA_ROOM, HGIC_RAW_MAX_PAYLOAD)

auto spidrv_read_buffer = std::make_unique<uint8_t[]>(MAX_SPI_RECEIVE_SIZE);

size_t Cycle_Time = 0;

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

// void spidrv_write(void *priv, unsigned char *data, unsigned int len, char dma_flag)
// {
//     // 软件模拟 SPI 写
//     for (unsigned int i = 0; i < len; ++i)
//     {
//         uint8_t byte = data[i];
//         for (int bit = 7; bit >= 0; --bit)
//         {
//             // 设置 MOSI
//             Tx_Ah_R900pnr_Spi_Bus->pin_write(TX_AH_R900PNR_MOSI, (byte >> bit) & 0x01);
//             // 拉低 SCLK
//             Tx_Ah_R900pnr_Spi_Bus->pin_write(TX_AH_R900PNR_SCLK, 0);
//             // 适当延时
//             Tx_Ah_R900pnr_Spi_Bus->delay_us(SOFTWARE_SPI_DELAY_US);
//             // 拉高 SCLK
//             Tx_Ah_R900pnr_Spi_Bus->pin_write(TX_AH_R900PNR_SCLK, 1);
//             // 适当延时
//             Tx_Ah_R900pnr_Spi_Bus->delay_us(SOFTWARE_SPI_DELAY_US);
//         }
//     }
// }

// void spidrv_read(void *priv, unsigned char *data, unsigned int len, char dma_flag)
// {
//     // 软件模拟 SPI 读
//     for (unsigned int i = 0; i < len; ++i)
//     {
//         uint8_t byte = 0;
//         for (int bit = 7; bit >= 0; --bit)
//         {
//             // 拉低 SCLK
//             Tx_Ah_R900pnr_Spi_Bus->pin_write(TX_AH_R900PNR_SCLK, 0);
//             // 适当延时
//             Tx_Ah_R900pnr_Spi_Bus->delay_us(SOFTWARE_SPI_DELAY_US);
//             // 拉高 SCLK
//             Tx_Ah_R900pnr_Spi_Bus->pin_write(TX_AH_R900PNR_SCLK, 1);
//             // 读取 MISO
//             int miso_val = Tx_Ah_R900pnr_Spi_Bus->pin_read(TX_AH_R900PNR_MISO);
//             byte |= (miso_val << bit);
//             // 适当延时
//             Tx_Ah_R900pnr_Spi_Bus->delay_us(SOFTWARE_SPI_DELAY_US);
//         }
//         data[i] = byte;
//     }
// }

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

    if (hgic_raw_get_fwinfo() == -1)
    {
        printf("hgic_raw_set_mode fail\n");
    }
    else
    {
        printf("hgic_raw_get_fwinfo success\n");
    }

    // if (hgic_raw_set_mode((char *)"ap") == -1)
    // {
    //     printf("hgic_raw_set_mode fail\n");
    // }
    // else
    // {
    //     printf("hgic_raw_set_mode success\n");
    // }

    // uart_flush_input(UART_NUM_0);

    while (1)
    {
        if (hgic_sdspi_detect_alive(0) == -1)
        {
            printf("hgic_sdspi_detect_alive fail hgic_sdspi_init start\n");
            hgic_sdspi_init(0);
        }

        if (Interrupt_Flag == true)
        {
            printf("Interrupt_Flag == true\n");
            auto buffer = std::make_unique<unsigned char[]>(1024);

            size_t length = hgic_sdspi_read(0, buffer.get(), 1024, 0);

            printf("spi receive length: %d\n", length);
            if (length != static_cast<size_t>(-1) && length > 0)
            {
                unsigned char *buffer_p = buffer.get();
                unsigned int length_2 = static_cast<unsigned int>(length);

                if (hgic_raw_rx(&buffer_p, &length_2) == HGIC_RAW_RX_TYPE_DATA)
                {
                    printf("spi receive data:[");
                    for (uint32_t i = 0; i < length_2; i++)
                    {
                        printf("%c", buffer_p[i]);
                    }
                    printf("]\n");
                }
            }

            Interrupt_Flag = false;
        }

        if (Tx_Ah_R900pnr_Spi_Bus->get_system_time_ms() > Cycle_Time)
        {
            printf("test data send start\n");

            if (hgic_raw_test2((unsigned char *)"0123456789", 10) == -1)
            {
                printf("hgic_raw_test2 fail\n");
            }

            // if (hgic_raw_get_fwinfo() == -1)
            // {
            //     printf("hgic_raw_get_fwinfo fail\n");
            // }

            Cycle_Time = Tx_Ah_R900pnr_Spi_Bus->get_system_time_ms() + 3000;
        }

        size_t uart_lenght = Uart_Bus_1->get_rx_buffer_length();
        if (uart_lenght > 0)
        {
            // 为了去除串口调试出现的乱码，这里+1预留最后一位'\0'
            std::unique_ptr<char[]> buffer = std::make_unique<char[]>(uart_lenght + 1);
            Uart_Bus_1->read(buffer.get(), uart_lenght);

            printf("uart receive lenght: %d\nuart receive: [%s]\n", uart_lenght, buffer.get());
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
