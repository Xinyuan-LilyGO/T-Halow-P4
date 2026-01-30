/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/**
 * @file
 * @brief ESP AMOLED: RM69A10
 */

#pragma once

#include "esp_lcd_panel_vendor.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create AMOLED control panel for RM69A10
 *
 * @param[in] io AMOLED panel IO handle
 * @param[in] panel_dev_config general panel device configuration
 * @param[out] ret_panel Returned AMOLED panel handle
 * @return
 *      - ESP_OK: Create AMOLED panel successfully
 *      - ESP_ERR_INVALID_ARG: Create AMOLED panel failed because of invalid arguments
 *      - ESP_ERR_NO_MEM: Create AMOLED panel failed because of memory allocation failure
 *      - ESP_FAIL: Create AMOLED panel failed because of other errors
 */
esp_err_t esp_lcd_new_panel_rm69a10(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel);

#ifdef __cplusplus
}
#endif
