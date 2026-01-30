/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2024-12-06 10:32:28
 * @LastEditTime: 2025-09-25 11:29:46
 */
#pragma once


// #define T_HALOW_V1_0
#define T_HALOW_V0_2

////////////////////////////////////////////////// gpio config //////////////////////////////////////////////////

#ifdef T_HALOW_V1_0 

// spi
#define SPI_1_SCLK 27
#define SPI_1_MOSI 26
#define SPI_1_MISO 25

//tx_ah_r900pnr
#define TX_AH_R900PNR_CS 24
#define TX_AH_R900PNR_INT 49
#define TX_AH_R900PNR_SCLK SPI_1_SCLK
#define TX_AH_R900PNR_MOSI SPI_1_MOSI
#define TX_AH_R900PNR_MISO SPI_1_MISO
#define TX_AH_R900PNR_DEBUG_UART_TX 51
#define TX_AH_R900PNR_DEBUG_UART_RX 52

// BOOT
#define ESP32P4_BOOT 35

#else

// spi
#define SPI_1_SCLK 43
#define SPI_1_MOSI 44
#define SPI_1_MISO 39

//tx_ah_r900pnr
#define TX_AH_R900PNR_CS 42
#define TX_AH_R900PNR_INT 40
#define TX_AH_R900PNR_SCLK SPI_1_SCLK
#define TX_AH_R900PNR_MOSI SPI_1_MOSI
#define TX_AH_R900PNR_MISO SPI_1_MISO
#define TX_AH_R900PNR_DEBUG_UART_TX 12
#define TX_AH_R900PNR_DEBUG_UART_RX 13

//T-Halow-P4
#define T_HALOW_P4_TX 37
#define T_HALOW_P4_RX 38

//sdio
//tx_ah_r900pnr
#define TX_AH_R900PNR_CMD 42
#define TX_AH_R900PNR_CLK 40
#define TX_AH_R900PNR_D3 42
#define TX_AH_R900PNR_D2 41
#define TX_AH_R900PNR_D1 40
#define TX_AH_R900PNR_D0 39

// BOOT
#define ESP32P4_BOOT 36

#define ECHO_TEST_TXD (GPIO_NUM_37)
#define ECHO_TEST_RXD (GPIO_NUM_38)
#define ECHO_TEST_RTS (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS (UART_PIN_NO_CHANGE)
#define ECHO_UART_PORT_NUM (UART_NUM_0)
#define ECHO_UART_BAUD_RATE (115200)
#define ECHO_TASK_STACK_SIZE (3072)

#define HALOW_TEST_TXD (GPIO_NUM_13)
#define HALOW_TEST_RXD (GPIO_NUM_12)
#define HALOW_TEST_RTS (UART_PIN_NO_CHANGE)
#define HALOW_TEST_CTS (UART_PIN_NO_CHANGE)
#define HALOW_UART_PORT_NUM (UART_NUM_1)
#define HALOW_UART_BAUD_RATE (115200)
#define HALOW_TASK_STACK_SIZE (3072)

#define HALOW_HOST SPI2_HOST
#define PIN_NUM_MISO GPIO_NUM_25
#define PIN_NUM_MOSI GPIO_NUM_26
#define PIN_NUM_CLK GPIO_NUM_27
#define PIN_NUM_CS GPIO_NUM_24
#define PIN_NUM_INT GPIO_NUM_49

#define PIN_BOOT GPIO_NUM_35


#endif

////////////////////////////////////////////////// gpio config //////////////////////////////////////////////////

////////////////////////////////////////////////// other define config //////////////////////////////////////////////////

////////////////////////////////////////////////// other define config //////////////////////////////////////////////////
