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
#include "t_halow_p4_board.h"
#include "cpp_bus_driver_library.h"
#include "esp_ldo_regulator.h"
// esp_brookesia
#include "esp_brookesia.hpp"
#include "esp_brookesia_phone_568_1232_stylesheet.h"
#include "app_examples/phone/simple_conf/src/phone_app_simple_conf.hpp"
#include "app_examples/phone/complex_conf/src/phone_app_complex_conf.hpp"
#include "app_examples/phone/squareline/src/phone_app_squareline.hpp"
// esp_brookesia app
#include "apps.h"

// -------------------------------------------------------------------
#define EXAMPLE_SHOW_MEM_INFO             (0)

static char * TAG = "app_main";

static void on_clock_update_timer_cb(struct _lv_timer_t *t);

auto IIC_Bus_1 = std::make_shared<Cpp_Bus_Driver::Hardware_Iic_1>(SGM38121_SDA, SGM38121_SCL, I2C_NUM_0);
auto SGM38121 = std::make_unique<Cpp_Bus_Driver::Sgm38121>(IIC_Bus_1, SGM38121_IIC_ADDRESS, DEFAULT_CPP_BUS_DRIVER_VALUE);

extern std::shared_ptr<Cpp_Bus_Driver::Hardware_Iic_1> IIC_Bus_1;


bool Init_Ldo_Channel_Power(uint8_t chan_id, uint32_t voltage_mv)
{
    esp_ldo_channel_handle_t ldo_channel_handle = NULL;
    esp_ldo_channel_config_t ldo_channel_config =
        {
            .chan_id = static_cast<int>(chan_id),
            .voltage_mv = static_cast<int>(voltage_mv),
        };
    if (esp_ldo_acquire_channel(&ldo_channel_config, &ldo_channel_handle) != ESP_OK)
    {
        printf("esp_ldo_acquire_channel %d fail\n", chan_id);
        return false;
    }

    return true;
}

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

    // halow_spi_test();

    vTaskDelay(pdMS_TO_TICKS(100));

    // SGM38121->begin();

    // SGM38121->set_output_voltage(Cpp_Bus_Driver::Sgm38121::Channel::DVDD_1, 1500);
    // SGM38121->set_output_voltage(Cpp_Bus_Driver::Sgm38121::Channel::AVDD_1, 1700);
    // SGM38121->set_output_voltage(Cpp_Bus_Driver::Sgm38121::Channel::AVDD_2, 3000);
    // SGM38121->set_channel_status(Cpp_Bus_Driver::Sgm38121::Channel::DVDD_1, Cpp_Bus_Driver::Sgm38121::Status::ON);
    // SGM38121->set_channel_status(Cpp_Bus_Driver::Sgm38121::Channel::AVDD_1, Cpp_Bus_Driver::Sgm38121::Status::ON);
    // SGM38121->set_channel_status(Cpp_Bus_Driver::Sgm38121::Channel::AVDD_2, Cpp_Bus_Driver::Sgm38121::Status::ON);

    // Init_Ldo_Channel_Power(3, 1830);

    vTaskDelay(pdMS_TO_TICKS(100));

    bsp_display_lock(0);

    /* LVGL demo */
    // lv_demo_benchmark();
    // lv_demo_music();
    // lv_demo_stress();

    /* Create a phone object */
    ESP_Brookesia_Phone *phone = new ESP_Brookesia_Phone(disp);
    ESP_BROOKESIA_CHECK_NULL_EXIT(phone, "Create phone failed");

    /* Try using a stylesheet that corresponds to the resolution */
    ESP_Brookesia_PhoneStylesheet_t *stylesheet = nullptr;
    if ((BSP_LCD_H_RES == 568) && (BSP_LCD_V_RES == 1232)) {
        stylesheet = new ESP_Brookesia_PhoneStylesheet_t ESP_BROOKESIA_PHONE_568_1232_DARK_STYLESHEET();
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
    phone->registerLvLockCallback((ESP_Brookesia_LvLockCallback_t)(bsp_display_lock), 0);
    phone->registerLvUnlockCallback((ESP_Brookesia_LvUnlockCallback_t)(bsp_display_unlock));
    ESP_BROOKESIA_CHECK_FALSE_EXIT(phone->begin(), "Begin failed");
    // ESP_BROOKESIA_CHECK_FALSE_EXIT(phone->getCoreHome().showContainerBorder(), "Show container border failed");

    /* Install squareline apps */
    PhoneAppSquareline *app_squareline = new PhoneAppSquareline(false, true);
    ESP_BROOKESIA_CHECK_NULL_EXIT(app_squareline, "Create app squareline failed");
    ESP_BROOKESIA_CHECK_FALSE_EXIT((phone->installApp(app_squareline) >= 0), "Install app squareline failed");
    /* Install calculate apps */
    Calculator *app_calculate = new Calculator(false, true);
    ESP_BROOKESIA_CHECK_NULL_EXIT(app_calculate, "Create app calculate conf failed");
    ESP_BROOKESIA_CHECK_FALSE_EXIT((phone->installApp(app_calculate) >= 0), "Install app calculate failed");
    /* Install game_2048 apps */
    Game2048 *app_game_2048 = new Game2048(false, true);
    ESP_BROOKESIA_CHECK_NULL_EXIT(app_game_2048, "Create app game_2048 conf failed");
    ESP_BROOKESIA_CHECK_FALSE_EXIT((phone->installApp(app_game_2048) >= 0), "Install app game_2048 failed");
    /* Install WIFI_Halow apps */
    WIFI_halow *app_wifi_halow = new WIFI_halow(false, true);
    ESP_BROOKESIA_CHECK_NULL_EXIT(app_wifi_halow, "Create app WIFI_Halow conf failed");
    ESP_BROOKESIA_CHECK_FALSE_EXIT((phone->installApp(app_wifi_halow) >= 0), "Install app WIFI_Halow failed");
    /* Install Camera apps */
    Camera *app_camera = new Camera(568, 1232);
    ESP_BROOKESIA_CHECK_NULL_EXIT(app_camera, "Create app Camera conf failed");
    ESP_BROOKESIA_CHECK_FALSE_EXIT((phone->installApp(app_camera) >= 0), "Install app Camera failed");
    /* Install setting apps */
    AppSettings *app_setting = new AppSettings(false, true);
    ESP_BROOKESIA_CHECK_NULL_EXIT(app_setting, "Create app setting conf failed");
    ESP_BROOKESIA_CHECK_FALSE_EXIT((phone->installApp(app_setting) >= 0), "Install app setting failed");

    PhoneAppSimpleConf *app_simple_conf = new PhoneAppSimpleConf();
    ESP_BROOKESIA_CHECK_NULL_EXIT(app_simple_conf, "Create app simple conf failed");
    ESP_BROOKESIA_CHECK_FALSE_EXIT((phone->installApp(app_simple_conf) >= 0), "Install app simple conf failed");
    PhoneAppComplexConf *app_complex_conf = new PhoneAppComplexConf();
    ESP_BROOKESIA_CHECK_NULL_EXIT(app_complex_conf, "Create app complex conf failed");
    ESP_BROOKESIA_CHECK_FALSE_EXIT((phone->installApp(app_complex_conf) >= 0), "Install app complex conf failed");

    /* Create a timer to update the clock */
    lv_timer_create(on_clock_update_timer_cb, 1000, phone);

    bsp_display_unlock();

#if EXAMPLE_SHOW_MEM_INFO
    char buffer[128];    /* Make sure buffer is enough for `sprintf` */
    size_t internal_free = 0;
    size_t internal_total = 0;
    size_t external_free = 0;
    size_t external_total = 0;
    while(1)
    {
        internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
        external_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        external_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        sprintf(buffer, "   Biggest /     Free /    Total\n"
                "\t  SRAM : [%8d / %8d / %8d]\n"
                "\t PSRAM : [%8d / %8d / %8d]",
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), internal_free, internal_total,
                heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM), external_free, external_total);
        ESP_LOGI("MEM", "%s", buffer);

        /**
         * The `lockLv()` and `unlockLv()` functions are used to lock and unlock the LVGL task.
         * They are registered by the `registerLvLockCallback()` and `registerLvUnlockCallback()` functions.
         */
        phone->lockLv();
        // Update memory label on "Recents Screen"
        if (!phone->getHome().getRecentsScreen()->setMemoryLabel(
                    internal_free / 1024, internal_total / 1024, external_free / 1024, external_total / 1024
                )) {
            ESP_LOGE(TAG, "Set memory label failed");
        }
        phone->unlockLv();

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
#endif
}

static void on_clock_update_timer_cb(struct _lv_timer_t *t)
{
    time_t now;
    struct tm timeinfo;
    bool is_time_pm = false;
    ESP_Brookesia_Phone *phone = (ESP_Brookesia_Phone *)t->user_data;

    time(&now);
    localtime_r(&now, &timeinfo);
    is_time_pm = (timeinfo.tm_hour >= 12);

    /* Since this callback is called from LVGL task, it is safe to operate LVGL */
    // Update clock on "Status Bar"
    ESP_BROOKESIA_CHECK_FALSE_EXIT(
        phone->getHome().getStatusBar()->setClock(timeinfo.tm_hour, timeinfo.tm_min, is_time_pm),
        "Refresh status bar failed"
    );

}

