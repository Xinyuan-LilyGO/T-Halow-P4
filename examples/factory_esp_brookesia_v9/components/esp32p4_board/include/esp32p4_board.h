
/**
 * @file
 * @brief ESP BSP: ESP32-P4 Function EV Board
 */

#pragma once

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/sdmmc_host.h"
#include "config.h"
#include "display.h"
#include "touch.h"
#include "sdkconfig.h"

#if (BSP_CONFIG_NO_GRAPHIC_LIB == 0)
#include "lvgl.h"
#include "esp_lvgl_port.h"
#endif // BSP_CONFIG_NO_GRAPHIC_LIB == 0

/**************************************************************************************************
 *  Kconfig
 **************************************************************************************************/
#define CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH 1
#define CONFIG_BSP_LCD_COLOR_FORMAT_RGB888 0
#define CONFIG_BSP_LCD_COLOR_FORMAT_RGB565 1
#define CONFIG_BSP_I2C_NUM 1
#define CONFIG_BSP_I2C_CLK_SPEED_HZ 400000


/**************************************************************************************************
 *  ESP pinout
 **************************************************************************************************/
/* I2C */
#define BSP_I2C_SCL           (GPIO_NUM_8)
#define BSP_I2C_SDA           (GPIO_NUM_7)

/* Display */
#define BSP_LCD_BACKLIGHT     (GPIO_NUM_NC)
#define BSP_LCD_RST           (GPIO_NUM_NC)
#define BSP_LCD_TOUCH_RST     (GPIO_NUM_NC)
#define BSP_LCD_TOUCH_INT     (GPIO_NUM_NC)

/* uSD card */
#define BSP_SD_D0             (GPIO_NUM_39)
#define BSP_SD_D1             (GPIO_NUM_40)
#define BSP_SD_D2             (GPIO_NUM_41)
#define BSP_SD_D3             (GPIO_NUM_42)
#define BSP_SD_CMD            (GPIO_NUM_44)
#define BSP_SD_CLK            (GPIO_NUM_43)

#ifdef __cplusplus
extern "C" {
#endif
/**************************************************************************************************
 *
 * I2C interface
 *
 * There are multiple devices connected to I2C peripheral:
 *  - Codec ES8311 (configuration only)
 *  - LCD Touch controller
 **************************************************************************************************/
#define BSP_I2C_NUM     CONFIG_BSP_I2C_NUM

/**
 * @brief Init I2C driver
 *
 * @return
 *      - ESP_OK                On success
 *      - ESP_ERR_INVALID_ARG   I2C parameter error
 *      - ESP_FAIL              I2C driver installation error
 *
 */
esp_err_t bsp_i2c_init(void);

/**
 * @brief Deinit I2C driver and free its resources
 *
 * @return
 *      - ESP_OK                On success
 *      - ESP_ERR_INVALID_ARG   I2C parameter error
 *
 */
esp_err_t bsp_i2c_deinit(void);

/**************************************************************************************************
 *
 * LCD interface
 *
 * ESP-BOX is shipped with 2.4inch ST7789 display controller.
 * It features 16-bit colors, 320x240 resolution and capacitive touch controller.
 *
 * LVGL is used as graphics library. LVGL is NOT thread safe, therefore the user must take LVGL mutex
 * by calling bsp_display_lock() before calling and LVGL API (lv_...) and then give the mutex with
 * bsp_display_unlock().
 *
 * Display's backlight must be enabled explicitly by calling bsp_display_backlight_on()
 **************************************************************************************************/
#if (BSP_CONFIG_NO_GRAPHIC_LIB == 0)

#define BSP_LCD_DRAW_BUFF_SIZE     (BSP_LCD_H_RES * 50) // Frame buffer size in pixels
#define BSP_LCD_DRAW_BUFF_DOUBLE   (0)

/**
 * @brief BSP display configuration structure
 *
 */
typedef struct {
    lvgl_port_cfg_t lvgl_port_cfg;  /*!< LVGL port configuration */
    uint32_t        buffer_size;    /*!< Size of the buffer for the screen in pixels */
    bool            double_buffer;  /*!< True, if should be allocated two buffers */
    struct {
        unsigned int buff_dma: 1;    /*!< Allocated LVGL buffer will be DMA capable */
        unsigned int buff_spiram: 1; /*!< Allocated LVGL buffer will be in PSRAM */
        unsigned int sw_rotate: 1;   /*!< Use software rotation (slower) */
    } flags;
} bsp_display_cfg_t;

/**
 * @brief Initialize display
 *
 * This function initializes SPI, display controller and starts LVGL handling task.
 * LCD backlight must be enabled separately by calling bsp_display_brightness_set()
 *
 * @return Pointer to LVGL display or NULL when error occured
 */
lv_display_t *bsp_display_start(void);

/**
 * @brief Initialize display
 *
 * This function initializes SPI, display controller and starts LVGL handling task.
 * LCD backlight must be enabled separately by calling bsp_display_brightness_set()
 *
 * @param cfg display configuration
 *
 * @return Pointer to LVGL display or NULL when error occured
 */
lv_display_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg);


/**
 * @brief Take LVGL mutex
 *
 * @param timeout_ms Timeout in [ms]. 0 will block indefinitely.
 * @return true  Mutex was taken
 * @return false Mutex was NOT taken
 */
bool bsp_display_lock(uint32_t timeout_ms);

/**
 * @brief Give LVGL mutex
 *
 */
void bsp_display_unlock(void);


int bsp_touchpad_read_point(int32_t *last_x, int32_t *last_y, int point_num);


lv_indev_t *bsp_display_indev_init(void);

lv_display_t *bsp_display_lcd_init(const bsp_display_cfg_t *cfg);
#endif // BSP_CONFIG_NO_GRAPHIC_LIB == 0


#ifdef __cplusplus
}
#endif
