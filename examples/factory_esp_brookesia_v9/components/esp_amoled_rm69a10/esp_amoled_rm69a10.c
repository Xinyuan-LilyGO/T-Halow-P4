/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <sys/cdefs.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_amoled_rm69a10.h"

#define ILI9881C_CMD_OPCODE (0x22)

#define ILI9881C_GS_PANEL   (1)
#define ILI9881C_SS_PANEL   (1 << 1)
#define ILI9881C_REV_PANEL  (1 << 2)
#define ILI9881C_BGR_PANEL  (1 << 3)

static const char *TAG = "rm69a10";

static esp_err_t panel_rm69a10_del(esp_lcd_panel_t *panel);
static esp_err_t panel_rm69a10_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_rm69a10_init(esp_lcd_panel_t *panel);
static esp_err_t panel_rm69a10_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t panel_rm69a10_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_rm69a10_swap_xy(esp_lcd_panel_t *panel, bool swap_axes);
static esp_err_t panel_rm69a10_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap);
static esp_err_t panel_rm69a10_disp_on_off(esp_lcd_panel_t *panel, bool off);
static esp_err_t panel_rm69a10_sleep(esp_lcd_panel_t *panel, bool sleep);

typedef struct {
    int cmd;           /*<! The specific LCD command */
    const void *data;  /*<! Buffer that holds the command specific data */
    size_t data_bytes; /*<! Size of `data` in memory, in bytes */
} rm69a10_lcd_init_cmd_t;

static const rm69a10_lcd_init_cmd_t vendor_specific_init_code_default[] = {
    // {cmd, { data }, data_size}
    /**** CMD_Page 3 ****/
    {0xFE, (uint8_t []) {0xFD}, 1},
    {0x80, (uint8_t []) {0xFC}, 1},
    {0xFE, (uint8_t []) {0x00}, 1},
    {0x2A, (uint8_t []) {0x00, 0x00, 0x02, 0x37}, 4},
    {0x2B, (uint8_t []) {0x00, 0x00, 0x04, 0xCF}, 4},
    {0x31, (uint8_t []) {0x00, 0x03, 0x02, 0x34}, 4},
    {0x30, (uint8_t []) {0x00, 0x00, 0x04, 0xCF}, 4},
    {0x12, (uint8_t []) {0x00}, 1},
    {0x35, (uint8_t []) {0x00}, 1},
    {0x51, (uint8_t []) {0xFE}, 1},
    {0x11, (uint8_t []) {0x00}, 0},
    {0x29, (uint8_t []) {0x00}, 0},
    //============ Gamma END===========
};

typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    int x_gap;
    int y_gap;
    uint8_t madctl_val; // save current value of LCD_CMD_MADCTL register
    uint8_t colmod_val; // save current value of LCD_CMD_COLMOD register
    uint16_t init_cmds_size;
    bool reset_level;
} rm69a10_panel_t;

esp_err_t esp_lcd_new_panel_rm69a10(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel)
{
    esp_err_t ret = ESP_OK;
    ESP_RETURN_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    rm69a10_panel_t *rm69a10 = (rm69a10_panel_t *)calloc(1, sizeof(rm69a10_panel_t));
    ESP_RETURN_ON_FALSE(rm69a10, ESP_ERR_NO_MEM, TAG, "no mem for rm69a10 panel");

    if (panel_dev_config->reset_gpio_num >= 0) {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
    }

    switch (panel_dev_config->rgb_ele_order) {
    case LCD_RGB_ELEMENT_ORDER_RGB:
        rm69a10->madctl_val = 0;
        break;
    case LCD_RGB_ELEMENT_ORDER_BGR:
        rm69a10->madctl_val |= LCD_CMD_BGR_BIT;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported rgb element order");
        break;
    }

    switch (panel_dev_config->bits_per_pixel) {
    case 16: // RGB565
        rm69a10->colmod_val = 0x55;
        break;
    case 18: // RGB666
        rm69a10->colmod_val = 0x66;
        break;
    case 24: // RGB888
        rm69a10->colmod_val = 0x77;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported pixel width");
        break;
    }

    // The ID register is on the CMD_Page 1
    // uint8_t ID1, ID2, ID3;
    // esp_lcd_panel_io_tx_param(io, 0xFF, (uint8_t[]) {
    //     0x98, 0x81, 0x01
    // }, 3);
    // esp_lcd_panel_io_rx_param(io, 0x00, &ID1, 1);
    // esp_lcd_panel_io_rx_param(io, 0x01, &ID2, 1);
    // esp_lcd_panel_io_rx_param(io, 0x02, &ID3, 1);
    // ESP_LOGI(TAG, "ID1: 0x%x, ID2: 0x%x, ID3: 0x%x", ID1, ID2, ID3);

    rm69a10->io = io;
    rm69a10->reset_gpio_num = panel_dev_config->reset_gpio_num;
    rm69a10->reset_level = panel_dev_config->flags.reset_active_high;
    rm69a10->base.del = panel_rm69a10_del;
    rm69a10->base.reset = panel_rm69a10_reset;
    rm69a10->base.init = panel_rm69a10_init;
    rm69a10->base.invert_color = panel_rm69a10_invert_color;
    rm69a10->base.set_gap = panel_rm69a10_set_gap;
    rm69a10->base.mirror = panel_rm69a10_mirror;
    rm69a10->base.swap_xy = panel_rm69a10_swap_xy;
    rm69a10->base.disp_on_off = panel_rm69a10_disp_on_off;
    rm69a10->base.disp_sleep = panel_rm69a10_sleep;
    *ret_panel = &rm69a10->base;

    return ESP_OK;

err:
    if (rm69a10) {
        panel_rm69a10_del(&rm69a10->base);
    }
    return ret;
}

static esp_err_t panel_rm69a10_del(esp_lcd_panel_t *panel)
{
    rm69a10_panel_t *rm69a10 = __containerof(panel, rm69a10_panel_t, base);

    if (rm69a10->reset_gpio_num >= 0) {
        gpio_reset_pin(rm69a10->reset_gpio_num);
    }
    free(rm69a10);
    return ESP_OK;
}

static esp_err_t panel_rm69a10_reset(esp_lcd_panel_t *panel)
{
    rm69a10_panel_t *rm69a10 = __containerof(panel, rm69a10_panel_t, base);
    esp_lcd_panel_io_handle_t io = rm69a10->io;

    // perform hardware reset
    if (rm69a10->reset_gpio_num >= 0) {
        gpio_set_level(rm69a10->reset_gpio_num, rm69a10->reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(rm69a10->reset_gpio_num, !rm69a10->reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
    } else { // perform software reset
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SWRESET, NULL, 0), TAG, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(20)); // spec, wait at least 5ms before sending new command
    }

    return ESP_OK;
}

static esp_err_t panel_rm69a10_init(esp_lcd_panel_t *panel)
{
    rm69a10_panel_t *rm69a10 = __containerof(panel, rm69a10_panel_t, base);
    esp_lcd_panel_io_handle_t io = rm69a10->io;

    const rm69a10_lcd_init_cmd_t *init_cmds = vendor_specific_init_code_default;
    uint16_t init_cmds_size = sizeof(vendor_specific_init_code_default) / sizeof(rm69a10_lcd_init_cmd_t);

    // back to CMD_Page 0
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, 0xFF, (uint8_t[]) {
        0x98, 0x81, 0x00
    }, 3), TAG, "send command failed");
    // exit sleep mode
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SLPOUT, NULL, 0), TAG,
                        "io tx param failed");
    vTaskDelay(pdMS_TO_TICKS(120));

    for (int i = 0; i < init_cmds_size; i++) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_bytes), TAG, "send command failed");
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]) {
        rm69a10->madctl_val,
    }, 1), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_COLMOD, (uint8_t[]) {
        rm69a10->colmod_val,
    }, 1), TAG, "send command failed");

    return ESP_OK;
}

static esp_err_t panel_rm69a10_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    rm69a10_panel_t *rm69a10 = __containerof(panel, rm69a10_panel_t, base);
    esp_lcd_panel_io_handle_t io = rm69a10->io;
    int command = 0;
    if (invert_color_data) {
        command = LCD_CMD_INVON;
    } else {
        command = LCD_CMD_INVOFF;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG, "send command failed");
    return ESP_OK;
}

static esp_err_t panel_rm69a10_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    rm69a10_panel_t *rm69a10 = __containerof(panel, rm69a10_panel_t, base);
    esp_lcd_panel_io_handle_t io = rm69a10->io;
    if (mirror_x) {
        rm69a10->madctl_val |= ILI9881C_SS_PANEL;
    } else {
        rm69a10->madctl_val &= ~ILI9881C_SS_PANEL;
    }
    if (mirror_y) {
        rm69a10->madctl_val |= ILI9881C_GS_PANEL;
    } else {
        rm69a10->madctl_val &= ~ILI9881C_GS_PANEL;
    }


    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]) {
        rm69a10->madctl_val
    }, 1), TAG, "send command failed");
    return ESP_OK;
}

static esp_err_t panel_rm69a10_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    ESP_LOGW(TAG, "Swap XY is not supported in ILI9881C driver. Please use SW rotation.");
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t panel_rm69a10_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    rm69a10_panel_t *rm69a10 = __containerof(panel, rm69a10_panel_t, base);
    rm69a10->x_gap = x_gap;
    rm69a10->y_gap = y_gap;
    return ESP_OK;
}

static esp_err_t panel_rm69a10_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    rm69a10_panel_t *rm69a10 = __containerof(panel, rm69a10_panel_t, base);
    esp_lcd_panel_io_handle_t io = rm69a10->io;
    int command = 0;

    if (on_off) {
        command = LCD_CMD_DISPON;
    } else {
        command = LCD_CMD_DISPOFF;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG, "send command failed");
    return ESP_OK;
}

static esp_err_t panel_rm69a10_sleep(esp_lcd_panel_t *panel, bool sleep)
{
    rm69a10_panel_t *rm69a10 = __containerof(panel, rm69a10_panel_t, base);
    esp_lcd_panel_io_handle_t io = rm69a10->io;
    int command = 0;
    if (sleep) {
        command = LCD_CMD_SLPIN;
    } else {
        command = LCD_CMD_SLPOUT;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG,
                        "io tx param failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    return ESP_OK;
}
