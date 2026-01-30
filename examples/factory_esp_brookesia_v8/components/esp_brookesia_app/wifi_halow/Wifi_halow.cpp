/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "Wifi_halow.hpp"
#include "esp_log.h"
#include "t_halow_p4_board.h"

#include "ui/ui.h"
#include "esp_brookesia_versions.h"
extern "C"
{
#include "hgic_sdspi.h"
#include "hgic_raw.h"
}
// #include "esp-bsp.h"

using namespace std;

LV_IMG_DECLARE(img_wifi_halow);

#define APP_NAME "WIFI Halow"

#define LVGL_FLUSH_BUF 1200
#define TEXT_SUM_MEX   1200

static lv_timer_t *halow_timer = NULL;
static uint32_t text_sum_len = 0;
static size_t Cycle_Time = 0;
TaskHandle_t halwo_echo_task111_handle = NULL;


extern volatile bool Interrupt_Flag;


void halow_timer_cb(lv_timer_t *t);

WIFI_halow::WIFI_halow(bool use_status_bar, bool use_navigation_bar) : ESP_Brookesia_PhoneApp(APP_NAME, &img_wifi_halow, false, use_status_bar, use_navigation_bar)
{
}

WIFI_halow::WIFI_halow() : ESP_Brookesia_PhoneApp(APP_NAME, &img_wifi_halow, false)
{
}

WIFI_halow::~WIFI_halow()
{
    ESP_BROOKESIA_LOGI("Destroy(@0x%p)", this);
}

bool WIFI_halow::run(void)
{
    // this->_core.
    ESP_BROOKESIA_LOGI("Run");

    ui_halow_init();

    halow_echo_resume();
    
    halow_timer = lv_timer_create(halow_timer_cb, 10, NULL);

    return true;
}

bool WIFI_halow::back(void)
{
    ESP_BROOKESIA_LOGI("Back");

    // If the app needs to exit, call notifyCoreClosed() to notify the core to close the app
    ESP_BROOKESIA_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");

    return true;
}

bool WIFI_halow::close(void)
{
    ESP_BROOKESIA_LOGI("Close");

    /* Do some operations here if needed */
    halow_echo_suspend();

    if (halow_timer)
    {
        lv_timer_del(halow_timer);
        halow_timer = NULL;
    }
    return true;
}

bool WIFI_halow::init()
{
    ESP_BROOKESIA_LOGI("Init");

    /* Do some initialization here if needed */

    halow_init();

    return true;
}

void halow_send_recv_loop(void)
{
    if (hgic_sdspi_detect_alive(0) == -1)
        {
            printf("hgic_sdspi_detect_alive fail hgic_sdspi_init start\n");
            hgic_sdspi_init(0);
        }
        if(Interrupt_Flag == true)
        {
            auto buffer = std::make_unique<unsigned char[]>(1024);

            size_t length = hgic_sdspi_read(0, buffer.get(), 1024, 0);

            if (length != static_cast<size_t>(-1) && length > 0)
            {
                unsigned char *buf_p = buffer.get();
                unsigned int length_2 = static_cast<unsigned int>(length);
                static int recv_cnt = 0;

                text_sum_len += length_2;
                if(text_sum_len > TEXT_SUM_MEX) {
                    text_sum_len = 0;
                    lv_textarea_set_text(ui_halowSetting_halowDebugTextArea, "");
                }
                
                if (hgic_raw_rx(&buf_p, &length_2) == HGIC_RAW_RX_TYPE_DATA)
                {
                    // printf("spi receive length: %d\n", length);
                    char buf[256];
                    int ret = 0;
                    // ret = lv_snprintf(buf, 256, " ------------------- [%d] receive %d data ------------------- \n", recv_cnt++, length);
                    // lv_textarea_set_text(ui_halowSetting_halowDebugTextArea, buf);

                    // ret = lv_snprintf(buf+ret, 256, "Destination address: %x-%x-%x-%x-%x-%x\n", buf_p[0], buf_p[1], buf_p[2],
                    //                                                    buf_p[3], buf_p[4], buf_p[5]);
                    // lv_textarea_set_text(ui_halowSetting_halowDebugTextArea, buf);
                    
                    // ret = lv_snprintf(buf+ret, 256, "Device address     : %x-%x-%x-%x-%x-%x\n", buf_p[6], buf_p[7], buf_p[8],
                    //                                                    buf_p[9], buf_p[10], buf_p[11]);
                    // lv_textarea_set_text(ui_halowSetting_halowDebugTextArea, buf);
                    
                    // ret = lv_snprintf(buf+ret, 256, "ETH type           : %x-%x\n", buf_p[12], buf_p[13]);
                    // lv_textarea_set_text(ui_halowSetting_halowDebugTextArea, buf);

                    // ret = lv_snprintf(buf+ret, 256, "Date: ");
                    // lv_textarea_set_text(ui_halowSetting_halowDebugTextArea, buf);

                    for (uint32_t i = 14; i < length_2; i++)
                    {
                        printf("%c", buf_p[i]);
                        buf[ret++] = buf_p[i];
                    }
                    buf[ret] = '\n';
                    lv_textarea_add_text(ui_halowSetting_halowDebugTextArea, buf);
                }
            }
            Interrupt_Flag = false;
        }
        if (esp_timer_get_time() / 1000 > Cycle_Time)
        {
            // printf("test data send start\n");

            // if (hgic_raw_test2((unsigned char *)"0123456789", 10) == -1)
            // {
            //     printf("hgic_raw_test2 fail\n");
            // }
#ifdef HALOW_DEFAULT_ROLE_STA 
            uint8_t dest[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
            static int count = 10000;
            std::string str = "STA" + std::to_string(count++);
            printf("test data send: %s\n", str.c_str());
            if(hgic_raw_send(dest, (unsigned char *)str.c_str(), str.length()) == -1)
            {
                printf("hgic_raw_send fail\n");
            }
            Cycle_Time = esp_timer_get_time() / 1000 + 1000;
#else 
            uint8_t dest[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
            static int count = 10000;
            std::string str = "AP " + std::to_string(count++);
            printf("test data send: %s\n", str.c_str());
            if(hgic_raw_send(dest, (unsigned char *)str.c_str(), str.length()) == -1)
            {
                printf("hgic_raw_send fail\n");
            }
            Cycle_Time = esp_timer_get_time() / 1000 + 3000;
#endif
        }
}

void halow_timer_cb(lv_timer_t *t)
{
    size_t item_size;
    uint8_t *item;
    static char merge_buf[LVGL_FLUSH_BUF];
    static uint16_t cnt = 0;
    int total = 0;

    while ((item = (uint8_t *)xRingbufferReceiveUpTo(
                log_rb, &item_size, 0, 128)) != NULL) {

        if (total + item_size >= LVGL_FLUSH_BUF)
            break;

        memcpy(&merge_buf[total], item, item_size);
        total += item_size;

        vRingbufferReturnItem(log_rb, item);
    }

    if (total > 0) {
        text_sum_len += total;
        if(text_sum_len > TEXT_SUM_MEX) {
            text_sum_len = 0;
            lv_textarea_set_text(ui_halowSetting_halowDebugTextArea, "");
        }
        merge_buf[total] = '\0';
        lv_textarea_add_text(ui_halowSetting_halowDebugTextArea, merge_buf);
    }

    if(cnt++ > 10) {
        cnt = 0;
        halow_send_recv_loop();
    }

    
}

// bool WIFI_halow::deinit()
// {
//     ESP_BROOKESIA_LOGD("Deinit");

//     /* Do some deinitialization here if needed */

//     return true;
// }

// bool WIFI_halow::pause()
// {
//     ESP_BROOKESIA_LOGD("Pause");

//     /* Do some operations here if needed */

//     return true;
// }

// bool WIFI_halow::resume()
// {
//     ESP_BROOKESIA_LOGD("Resume");

//     /* Do some operations here if needed */

//     return true;
// }

// bool WIFI_halow::cleanResource()
// {
//     ESP_BROOKESIA_LOGD("Clean resource");

//     /* Do some cleanup here if needed */

//     return true;
// }
