/*
 * Config web server: serves the embedded configurator page and bridges
 * POST /api/at onto the HaLow module's AT UART.
 *
 * License: GPL 3.0
 */
#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t web_server_start(void);

/*
 * The HaLow role ("ap"/"sta") last set through the config page, from NVS.
 * `role` is left untouched when nothing has been saved -- prime it with the
 * default. The module reverts its role every power-up, so app_main asserts
 * this value each boot.
 */
void web_server_get_halow_role(char *role, size_t cap);

#ifdef __cplusplus
}
#endif
