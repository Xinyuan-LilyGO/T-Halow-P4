#pragma once
#include "esp_lcd_types.h"
#include "esp_lcd_mipi_dsi.h"
#include "sdkconfig.h"


/* LCD display color bytes endianess */
#define BSP_LCD_BIGENDIAN           (0)
/* LCD display color bits */
#define BSP_LCD_BITS_PER_PIXEL      (16)
/* LCD display color space */
#define BSP_LCD_COLOR_SPACE         (ESP_LCD_COLOR_SPACE_RGB)

/* LCD display definition  */
#define BSP_LCD_H_RES              (568)
#define BSP_LCD_V_RES              (1232)

/* LCD pixel color */
// 80000000/(80+50+150+568)/(40+120+80+1232) = 64Hz
#define BSP_MIPI_DSI_DPI_CLK_MHZ      (80)
#define BSP_LCD_MIPI_DSI_LCD_HSYNC    (50)
#define BSP_LCD_MIPI_DSI_LCD_HBP      (150)
#define BSP_LCD_MIPI_DSI_LCD_HFP      (50)
#define BSP_LCD_MIPI_DSI_LCD_VSYNC    (40)
#define BSP_LCD_MIPI_DSI_LCD_VBP      (120)
#define BSP_LCD_MIPI_DSI_LCD_VFP      (80)

#define BSP_LCD_MIPI_DSI_LANE_NUM          (2)    // 2 data lanes
#define BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS (1000) // 1Gbps

#define BSP_MIPI_DSI_PHY_PWR_LDO_CHAN       (3)  // LDO_VO3 is connected to VDD_MIPI_DPHY
#define BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV (2500)


#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief BSP display configuration structure
 *
 */
typedef struct {
    int dummy;
} bsp_display_config_t;


/**
 * @brief BSP display return handles
 *
 */
typedef struct {
    esp_lcd_dsi_bus_handle_t    mipi_dsi_bus;  /*!< MIPI DSI bus handle */
    esp_lcd_panel_io_handle_t   io;            /*!< ESP LCD IO handle */
    esp_lcd_panel_handle_t      panel;         /*!< ESP LCD panel (color) handle */
    esp_lcd_panel_handle_t      control;       /*!< ESP LCD panel (control) handle */
} bsp_lcd_handles_t;

#ifdef __cplusplus
}
#endif
