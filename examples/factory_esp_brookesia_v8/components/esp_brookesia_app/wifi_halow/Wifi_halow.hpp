#pragma once

#include "lvgl.h"
#include "esp_brookesia.hpp"


class WIFI_halow: public ESP_Brookesia_PhoneApp {
public:

    WIFI_halow(bool use_status_bar, bool use_navigation_bar);
    WIFI_halow();
    ~WIFI_halow();

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;

    bool init(void) override;
    // bool init(void) override;

    // bool pause(void) override;
    // bool resume(void) override;
    // bool cleanResource(void) override;
};
