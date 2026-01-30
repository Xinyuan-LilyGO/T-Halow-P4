#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "lv_demos.h"
#include "lv_examples.h"

// esp32p4_board
#include "esp-bsp.h"
#include "display.h"

// esp_brookesia
#include "esp_brookesia.hpp"
#include "private/esp_brookesia_utils.h"
#include "esp_brookesia_phone_568_1232_stylesheet.hpp"

static char * TAG = "main";

extern "C" void app_main(void)
{
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = false,
        }};
    // lv_disp_t *disp = bsp_display_start_with_config(&cfg);


    lvgl_port_init(&cfg.lvgl_port_cfg);

    lv_disp_t *disp = bsp_display_lcd_init(&cfg);

    lv_indev_t *indev = bsp_display_indev_init();

    // bsp_display_backlight_on();  // AMOLED no backlight

    bsp_display_lock(0);

    /* LVGL demo */
    // lv_demo_benchmark();
    // lv_demo_music();
    // lv_demo_stress();
    

    // 创建一个简单的 LVGL 界面
    // lv_obj_t *scr = lv_scr_act();
    // lv_obj_t *label = lv_label_create(scr);
    // lv_label_set_text(label, "Hello, LVGL with ESP32!");
    // lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    ESP_Brookesia_Phone *phone = new ESP_Brookesia_Phone(disp);
    ESP_BROOKESIA_CHECK_NULL_EXIT(phone, "Create phone failed");

    /* Try using a stylesheet that corresponds to the resolution */
    ESP_Brookesia_PhoneStylesheet_t *stylesheet = nullptr;
    if ((BSP_LCD_H_RES == 568) && (BSP_LCD_V_RES == 1232)) {
        stylesheet = new ESP_Brookesia_PhoneStylesheet_t(ESP_BROOKESIA_PHONE_568_1232_DARK_STYLESHEET());
        ESP_BROOKESIA_CHECK_NULL_EXIT(stylesheet, "Create stylesheet failed");
    } else if ((BSP_LCD_H_RES == 800) && (BSP_LCD_V_RES == 1280)) {
        stylesheet = new ESP_Brookesia_PhoneStylesheet_t(ESP_BROOKESIA_PHONE_800_1280_DARK_STYLESHEET());
        ESP_BROOKESIA_CHECK_NULL_EXIT(stylesheet, "Create stylesheet failed");
    }
    if (stylesheet != nullptr) {
        ESP_LOGI(TAG, "Using stylesheet (%s)", stylesheet->core.name);
        ESP_BROOKESIA_CHECK_FALSE_EXIT(phone->addStylesheet(stylesheet), "Add stylesheet failed");
        ESP_BROOKESIA_CHECK_FALSE_EXIT(phone->activateStylesheet(stylesheet), "Activate stylesheet failed");
        delete stylesheet;
    }

    /* Configure and begin the phone */
    ESP_BROOKESIA_CHECK_FALSE_EXIT(phone->setTouchDevice(indev), "Set touch device failed");
    phone->registerLvLockCallback((ESP_Brookesia_GUI_LockCallback_t)(bsp_display_lock), 0);
    phone->registerLvUnlockCallback((ESP_Brookesia_GUI_UnlockCallback_t)(bsp_display_unlock));
    ESP_BROOKESIA_CHECK_FALSE_EXIT(phone->begin(), "Begin failed");
    ESP_BROOKESIA_CHECK_FALSE_EXIT(phone->getCoreHome().showContainerBorder(), "Show container border failed");

    /* Install apps */
    PhoneAppSimpleConf *app_simple_conf = new PhoneAppSimpleConf();
    ESP_BROOKESIA_CHECK_NULL_EXIT(app_simple_conf, "Create app simple conf failed");
    ESP_BROOKESIA_CHECK_FALSE_EXIT((phone->installApp(app_simple_conf) >= 0), "Install app simple conf failed");
    PhoneAppComplexConf *app_complex_conf = new PhoneAppComplexConf();
    ESP_BROOKESIA_CHECK_NULL_EXIT(app_complex_conf, "Create app complex conf failed");
    ESP_BROOKESIA_CHECK_FALSE_EXIT((phone->installApp(app_complex_conf) >= 0), "Install app complex conf failed");
    PhoneAppSquareline *app_squareline = PhoneAppSquareline::getInstance();
    ESP_BROOKESIA_CHECK_NULL_EXIT(app_squareline, "Create app squareline failed");
    ESP_BROOKESIA_CHECK_FALSE_EXIT((phone->installApp(app_squareline) >= 0), "Install app squareline failed");


    bsp_display_unlock();
}
