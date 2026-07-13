/*
 * IP networking over Wi-Fi HaLow on the T-Halow-P4.
 *
 * Derived from examples/halow_spi_single (Author: LILYGO_L).
 * License: GPL 3.0
 */
#include <cstring>
#include <memory>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_rom_uart.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "t_halow_p4_config.h"
#include "cpp_bus_driver_library.h"
#include "hgic_netif.h"

extern "C"
{
#include "hgic_sdspi.h"
#include "hgic_raw.h"
}

#define MAX_SPI_RECEIVE_SIZE std::min(HGIC_RAW_DATA_ROOM, HGIC_RAW_MAX_PAYLOAD)
#define UART_MONITOR_BUF_SIZE 1024
#define HGIC_RX_BUF_SIZE 2048

auto spidrv_read_buffer = std::make_unique<uint8_t[]>(MAX_SPI_RECEIVE_SIZE);

volatile bool Interrupt_Flag = false;

static esp_netif_t *s_halow_netif = nullptr;

auto Tx_Ah_R900pnr_Spi_Bus = std::make_shared<Cpp_Bus_Driver::Hardware_Spi>(
    TX_AH_R900PNR_MOSI, TX_AH_R900PNR_SCLK, TX_AH_R900PNR_MISO,
    SPI2_HOST, 0, spi_clock_source_t::SPI_CLK_SRC_SPLL);

// ---------------------------------------------------------------------------
// SPI glue required by the vendor driver.
// raw_send() is renamed to hgic_spi_raw_send() by a compile definition on the
// vendor component, because lwIP exports a raw_send() of its own.
// ---------------------------------------------------------------------------

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
    // CS is asserted by the SPI peripheral for the duration of each transfer.
}

int spidrv_hw_crc(void *priv, unsigned char *data, unsigned int len, char flag)
{
    return 0;
}

int raw_send(unsigned char *data, unsigned int len)
{
    return hgic_sdspi_write(0, data, len);
}

// ---------------------------------------------------------------------------

// Drain the module's SPI queue. The interrupt is edge-triggered on FALLING, so
// reading only once per interrupt strands any frame that arrived while the
// previous one was being serviced: the line is already low, so no further edge
// is generated and the queue stalls.
static void hgic_pump_rx(void)
{
    static auto rx = std::make_unique<unsigned char[]>(HGIC_RX_BUF_SIZE);

    for (int i = 0; i < 8; i++)
    {
        size_t length = hgic_sdspi_read(0, rx.get(), HGIC_RX_BUF_SIZE, 0);
        if (length == static_cast<size_t>(-1) || length == 0)
        {
            break;
        }

        unsigned char *p = rx.get();
        unsigned int len = static_cast<unsigned int>(length);

        // hgic_raw_rx() dispatches command responses and events internally, and
        // for data it advances p past the hgic header onto the ethernet frame.
        if (hgic_raw_rx(&p, &len) == HGIC_RAW_RX_TYPE_DATA && s_halow_netif != nullptr)
        {
            hgic_netif_input(s_halow_netif, p, len);
        }
    }
}

// hgic_raw_get_fwinfo() only writes the request; hgic.addr is populated later,
// when hgic_raw_rx() consumes the response. Pump RX until the MAC turns up.
static bool hgic_wait_for_mac(int timeout_ms)
{
    const uint8_t zero[6] = {0};
    int64_t deadline = esp_timer_get_time() / 1000 + timeout_ms;
    int64_t next_request = 0;

    while (esp_timer_get_time() / 1000 < deadline)
    {
        int64_t now = esp_timer_get_time() / 1000;
        if (now >= next_request)
        {
            hgic_raw_get_fwinfo();
            next_request = now + 500;
        }

        hgic_pump_rx();
        if (memcmp(hgic.addr, zero, 6) != 0)
        {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

// Bridge the USB console UART to the HaLow module's AT UART. This is the only
// path by which AT commands reach the radio over USB-C, so keep it.
static void at_bridge_task(void *arg)
{
    // The console shares ECHO_UART_PORT_NUM, and uart_driver_install() resets
    // its TX FIFO. Anything still queued -- typically the last few log lines
    // from app_main() -- would be silently discarded. Drain it first.
    fflush(stdout);
    esp_rom_uart_tx_wait_idle(ECHO_UART_PORT_NUM);

    uart_config_t console_config = {
        .baud_rate = ECHO_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(ECHO_UART_PORT_NUM, UART_MONITOR_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(ECHO_UART_PORT_NUM, &console_config));
    ESP_ERROR_CHECK(uart_set_pin(ECHO_UART_PORT_NUM, ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_RTS, ECHO_TEST_CTS));

    uart_config_t halow_config = console_config;
    halow_config.baud_rate = HALOW_UART_BAUD_RATE;
    ESP_ERROR_CHECK(uart_driver_install(HALOW_UART_PORT_NUM, UART_MONITOR_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(HALOW_UART_PORT_NUM, &halow_config));
    ESP_ERROR_CHECK(uart_set_pin(HALOW_UART_PORT_NUM, HALOW_TEST_TXD, HALOW_TEST_RXD, HALOW_TEST_RTS, HALOW_TEST_CTS));

    uint8_t *data = (uint8_t *)malloc(UART_MONITOR_BUF_SIZE);

    while (1)
    {
        int len = uart_read_bytes(ECHO_UART_PORT_NUM, data, UART_MONITOR_BUF_SIZE - 1, pdMS_TO_TICKS(20));
        if (len > 0)
        {
            uart_write_bytes(HALOW_UART_PORT_NUM, (const char *)data, len);
        }

        len = uart_read_bytes(HALOW_UART_PORT_NUM, data, UART_MONITOR_BUF_SIZE - 1, pdMS_TO_TICKS(20));
        if (len > 0)
        {
            uart_write_bytes(ECHO_UART_PORT_NUM, (const char *)data, len);
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

extern "C" void app_main(void)
{
    Tx_Ah_R900pnr_Spi_Bus->create_gpio_interrupt(
        TX_AH_R900PNR_INT, Cpp_Bus_Driver::Tool::Interrupt_Mode::FALLING,
        [](void *arg) -> IRAM_ATTR void
        {
            Interrupt_Flag = true;
        });

    memset(spidrv_read_buffer.get(), 0xFF, MAX_SPI_RECEIVE_SIZE);

    // The TX-AH module requires a 40 MHz SPI clock.
    Tx_Ah_R900pnr_Spi_Bus->begin(40000000, TX_AH_R900PNR_CS);

    // Give the module's SPI slave time to come up.
    vTaskDelay(pdMS_TO_TICKS(1000));

    while (hgic_sdspi_init(0) == -1)
    {
        printf("hgic_sdspi_init fail, retrying\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    printf("hgic_sdspi_init success\n");

    if (!hgic_wait_for_mac(5000))
    {
        printf("hgic_raw_get_fwinfo: no response, module mac unknown\n");
        return;
    }

#if CONFIG_HALOW_ROLE_AP
    hgic_raw_set_mode((char *)"ap");
#else
    hgic_raw_set_mode((char *)"sta");
#endif

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_halow_netif = hgic_netif_create(hgic.addr);
    if (s_halow_netif == nullptr)
    {
        printf("hgic_netif_create fail\n");
        return;
    }

#if CONFIG_HALOW_USE_DHCP
    ESP_ERROR_CHECK(hgic_netif_use_dhcp(s_halow_netif));
#else
    ESP_ERROR_CHECK(hgic_netif_set_static_ip(s_halow_netif, CONFIG_HALOW_STATIC_IP,
                                             CONFIG_HALOW_STATIC_NETMASK,
                                             CONFIG_HALOW_STATIC_GW));
#endif

    xTaskCreate(at_bridge_task, "at_bridge", 3072, NULL, 10, NULL);

    // Seed hgic.status.conn_state; the response is parsed by hgic_raw_rx().
    hgic_raw_get_connect_state();

    int last_link = -1;

    while (1)
    {
        if (hgic_sdspi_detect_alive(0) == -1)
        {
            printf("hgic_sdspi_detect_alive fail, re-initialising\n");
            hgic_sdspi_init(0);
        }

        if (Interrupt_Flag)
        {
            Interrupt_Flag = false;
            hgic_pump_rx();
        }

        if (hgic.status.conn_state != last_link)
        {
            last_link = hgic.status.conn_state;
            hgic_netif_set_link(s_halow_netif, last_link != 0);
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
