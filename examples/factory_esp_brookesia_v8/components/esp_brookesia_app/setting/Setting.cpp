/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cmath>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_check.h"
#include "esp_memory_utils.h"
#include "esp_mac.h"
// #include "bsp/esp-bsp.h"
// #include "bsp_board_extra.h"
#include "nvs.h"

#include "ui/ui.h"
#include "Setting.hpp"
#include "app_sntp.h"

#include "esp_brookesia_versions.h"

#define ENABLE_DEBUG_LOG                (0)

#define HOME_REFRESH_TASK_STACK_SIZE    (1024 * 4)
#define HOME_REFRESH_TASK_PRIORITY      (1)
#define HOME_REFRESH_TASK_PERIOD_MS     (2000)

#define WIFI_SCAN_TASK_STACK_SIZE       (1024 * 6)
#define WIFI_SCAN_TASK_PRIORITY         (1)
#define WIFI_SCAN_TASK_PERIOD_MS        (5 * 1000)

#define WIFI_CONNECT_TASK_STACK_SIZE    (1024 * 4)
#define WIFI_CONNECT_TASK_PRIORITY      (4)
#define WIFI_CONNECT_TASK_STACK_CORE    (0)
#define WIFI_CONNECT_UI_WAIT_TIME_MS    (1 * 1000)
#define WIFI_CONNECT_UI_PANEL_SIZE      (1 * 1000)
#define WIFI_CONNECT_RET_WAIT_TIME_MS   (10 * 1000)

#define SCREEN_BRIGHTNESS_MIN           (20)
#define SCREEN_BRIGHTNESS_MAX           (BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX)

#define SPEAKER_VOLUME_MIN              (0)
#define SPEAKER_VOLUME_MAX              (100)

#define NVS_STORAGE_NAMESPACE           "storage"
#define NVS_KEY_WIFI_ENABLE             "wifi_en"
#define NVS_KEY_BLE_ENABLE              "ble_en"
#define NVS_KEY_AUDIO_VOLUME            "volume"
#define NVS_KEY_DISPLAY_BRIGHTNESS      "brightness"

#define UI_MAIN_ITEM_LEFT_OFFSET        (20)
#define UI_WIFI_LIST_UP_OFFSET          (20)
#define UI_WIFI_LIST_UP_PAD             (20)
#define UI_WIFI_LIST_DOWN_PAD           (20)
#define UI_WIFI_LIST_H_PERCENT          (75)
#define UI_WIFI_LIST_ITEM_H             (60)
#define UI_WIFI_LIST_ITEM_FONT          (&lv_font_montserrat_26)
#define UI_WIFI_KEYBOARD_H_PERCENT      (30)
#define UI_WIFI_ICON_LOCK_RIGHT_OFFSET       (-10)
#define UI_WIFI_ICON_SIGNAL_RIGHT_OFFSET     (-50)
#define UI_WIFI_ICON_CONNECT_RIGHT_OFFSET    (-90)

using namespace std;

#define SCAN_LIST_SIZE      25

#define APP_NAME                "Settings"

static const char TAG[] = "EUI_Setting";

TaskHandle_t wifi_scan_handle_task;

static EventGroupHandle_t s_wifi_event_group;

static char st_wifi_ssid[32];
static char st_wifi_password[64];

static uint8_t base_mac_addr[6] = {0};
static char mac_str[18] = {0};

static lv_obj_t* panel_wifi_btn[SCAN_LIST_SIZE];
static lv_obj_t* label_wifi_ssid[SCAN_LIST_SIZE];
static lv_obj_t* img_img_wifi_lock[SCAN_LIST_SIZE];
static lv_obj_t* wifi_image[SCAN_LIST_SIZE];
static lv_obj_t* wifi_connect[SCAN_LIST_SIZE];

static int brightness;

LV_IMG_DECLARE(img_wifisignal_absent);
LV_IMG_DECLARE(img_wifisignal_wake);
LV_IMG_DECLARE(img_wifisignal_moderate);
LV_IMG_DECLARE(img_wifisignal_good);
LV_IMG_DECLARE(img_wifi_lock);
LV_IMG_DECLARE(img_wifi_connect_success);
LV_IMG_DECLARE(img_wifi_connect_fail);

typedef enum {
    WIFI_EVENT_CONNECTED = BIT(0),
    WIFI_EVENT_INIT_DONE = BIT(1),
    WIFI_EVENT_UI_INIT_DONE = BIT(2),
    WIFI_EVENT_SCANING = BIT(3)
} wifi_event_id_t;

LV_IMG_DECLARE(img_app_setting);
extern lv_obj_t *ui_Min;
extern lv_obj_t *ui_Hour;
extern lv_obj_t *ui_Sec;
extern lv_obj_t *ui_Date;
extern lv_obj_t *ui_Clock_Number;

AppSettings::AppSettings(bool use_status_bar, bool use_navigation_bar):
    ESP_Brookesia_PhoneApp(APP_NAME, &img_app_setting, false, use_status_bar, use_navigation_bar),                  // auto_resize_visual_area
    _is_ui_resumed(false),
    _is_ui_del(true),
    _screen_index(UI_MAIN_SETTING_INDEX),
    _screen_list({nullptr})
{
}

AppSettings::AppSettings():
    ESP_Brookesia_PhoneApp(APP_NAME, &img_app_setting, false),                  // auto_resize_visual_area
    _is_ui_resumed(false),
    _is_ui_del(true),
    _screen_index(UI_MAIN_SETTING_INDEX),
    _screen_list({nullptr})
{
}

AppSettings::~AppSettings()
{
}

bool AppSettings::run(void)
{
    // _is_ui_del = false;

    // Initialize Squareline UI
    ui_setting_init();

    // Get MAC
    // esp_read_mac(base_mac_addr, ESP_MAC_EFUSE_FACTORY);
    // snprintf(mac_str, sizeof(mac_str), "%02X-%02X-%02X-%02X-%02X-%02X",
    //          base_mac_addr[0], base_mac_addr[1], base_mac_addr[2],
    //          base_mac_addr[3], base_mac_addr[4], base_mac_addr[5]);


    // Initialize custom UI
    // extraUiInit();

    // Upate UI by NVS parametres
    // updateUiByNvsParam();

    // xEventGroupSetBits(s_wifi_event_group, WIFI_EVENT_UI_INIT_DONE);

    return true;
}

bool AppSettings::back(void)
{
    notifyCoreClosed();

    return true;
}

