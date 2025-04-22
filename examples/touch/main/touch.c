#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"

#define BSP_I2C_NUM     1
#define BSP_I2C_SCL     (GPIO_NUM_8)
#define BSP_I2C_SDA     (GPIO_NUM_7)

#define BSP_I2C_CLK_SPEED_HZ        400000
#define BSP_LCD_H_RES               (1024)
#define BSP_LCD_V_RES               (600)

#define BSP_LCD_TOUCH_RST     (GPIO_NUM_NC)
#define BSP_LCD_TOUCH_INT     (GPIO_NUM_NC)

static const char *TAG = "Touch";

esp_lcd_touch_handle_t touch = NULL;

static bool i2c_initialized = false;
static i2c_master_bus_handle_t i2c_handle = NULL;  // I2C Handle

esp_err_t bsp_i2c_init(void)
{
    // /* I2C was initialized before */
    if (i2c_initialized) {
        return ESP_OK;
    }

    i2c_master_bus_config_t i2c_bus_conf = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .i2c_port = BSP_I2C_NUM,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_conf, &i2c_handle));

    i2c_initialized = true;

    return ESP_OK;
}

esp_err_t bsp_touch_new(esp_lcd_touch_handle_t *ret_touch)
{
    /* Initilize I2C */
    ESP_ERROR_CHECK(bsp_i2c_init());

    /* Initialize touch */
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = BSP_LCD_TOUCH_RST, // Shared with LCD reset
        .int_gpio_num = BSP_LCD_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 1,
            .mirror_y = 1,
        },
    };
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_config.scl_speed_hz = BSP_I2C_CLK_SPEED_HZ;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_handle, &tp_io_config, &tp_io_handle));
    return esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, ret_touch);
}

static void touch_read(void *args)
{
    uint16_t touchpad_x[1] = {0};
    uint16_t touchpad_y[1] = {0};
    uint8_t touchpad_cnt = 0;
    bool ret = esp_lcd_touch_read_data(touch);
    bool pressed = esp_lcd_touch_get_coordinates(touch, touchpad_x, touchpad_y, NULL, &touchpad_cnt, 1);
    while (1)
    {
        ret = esp_lcd_touch_read_data(touch);
        pressed = esp_lcd_touch_get_coordinates(touch, touchpad_x, touchpad_y, NULL, &touchpad_cnt, 1);
        if(pressed)
        {
            ESP_LOGI(TAG, "r=%d, p=%d, t=%d, x=%d, y=%d", ret, pressed, touchpad_cnt, touchpad_x[0], touchpad_y[0]);
        }
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "touch=0x%p", touch);
    ESP_ERROR_CHECK(bsp_touch_new(&touch));
    ESP_LOGI(TAG, "touch=0x%p", touch);

    xTaskCreate(touch_read, "touch_read", 8192, NULL, 5, NULL);
}
