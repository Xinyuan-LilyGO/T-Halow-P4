/*
 * License: GPL 3.0
 */
#include "at_uart.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_timer.h"

#include "t_halow_p4_config.h"

#define AT_UART_RX_BUF_SIZE 4096
#define AT_UART_MUTEX_TIMEOUT_MS 10000

static SemaphoreHandle_t s_mutex;

esp_err_t at_uart_init(void)
{
    uart_config_t config = {
        .baud_rate = HALOW_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(uart_driver_install(HALOW_UART_PORT_NUM, AT_UART_RX_BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(HALOW_UART_PORT_NUM, &config));
    ESP_ERROR_CHECK(uart_set_pin(HALOW_UART_PORT_NUM, HALOW_TEST_TXD, HALOW_TEST_RXD,
                                 HALOW_TEST_RTS, HALOW_TEST_CTS));
    return ESP_OK;
}

/* Collect until quiet. Caller holds the mutex. */
static int collect(int quiet_ms, int max_ms, char *out, size_t out_sz)
{
    int64_t start = esp_timer_get_time() / 1000;
    int64_t last = start;
    size_t used = 0;

    while (esp_timer_get_time() / 1000 - start < max_ms && used < out_sz - 1)
    {
        int len = uart_read_bytes(HALOW_UART_PORT_NUM, (uint8_t *)out + used,
                                  out_sz - 1 - used, pdMS_TO_TICKS(25));
        if (len > 0)
        {
            used += len;
            last = esp_timer_get_time() / 1000;
        }
        else if (used > 0 && esp_timer_get_time() / 1000 - last > quiet_ms)
        {
            break;
        }
    }
    out[used] = '\0';
    return (int)used;
}

int at_uart_cmd(const char *line, int quiet_ms, int max_ms, char *out, size_t out_sz)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(AT_UART_MUTEX_TIMEOUT_MS)) != pdTRUE)
    {
        return -1;
    }

    uart_flush_input(HALOW_UART_PORT_NUM);
    uart_write_bytes(HALOW_UART_PORT_NUM, line, strlen(line));
    uart_write_bytes(HALOW_UART_PORT_NUM, "\r\n", 2);

    int used = collect(quiet_ms, max_ms, out, out_sz);

    xSemaphoreGive(s_mutex);
    return used;
}

bool at_uart_resync(void)
{
    static char reply[512];
    static uint8_t filler[1700];

    memset(filler, 0x55, sizeof(filler));

    for (int i = 0; i < 5; i++)
    {
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(AT_UART_MUTEX_TIMEOUT_MS)) != pdTRUE)
        {
            return false;
        }
        uart_write_bytes(HALOW_UART_PORT_NUM, filler, sizeof(filler));
        vTaskDelay(pdMS_TO_TICKS(200));
        uart_flush_input(HALOW_UART_PORT_NUM);
        xSemaphoreGive(s_mutex);

        if (at_uart_cmd("AT+MODE", 250, 1200, reply, sizeof(reply)) > 0 &&
            strstr(reply, "+MODE") != NULL)
        {
            return true;
        }
    }
    return false;
}

void at_uart_quiet(void)
{
    static char reply[256];
    at_uart_cmd("AT+SYSDBG=LMAC,0", 200, 900, reply, sizeof(reply));
    at_uart_cmd("AT+SYSDBG=WNB,0", 200, 900, reply, sizeof(reply));
}
