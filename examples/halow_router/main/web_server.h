/*
 * Config web server: serves the embedded configurator page and bridges
 * POST /api/at onto the HaLow module's AT UART.
 *
 * License: GPL 3.0
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t web_server_start(void);

#ifdef __cplusplus
}
#endif
