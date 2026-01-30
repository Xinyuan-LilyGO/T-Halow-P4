#ifndef __SYS_PROJECT_CONFIG_H__
#define __SYS_PROJECT_CONFIG_H__
#define SYSCFG_ENABLE
#define WNB_STA_COUNT (8)
#define OTA_ENABLE
#define WNB_FRM_TYPE 1
#define ASSERT_HOLDUP 1

#define MACBUS_SDIO
//#define MACBUS_USB
//#define MACBUS_UART

#define MEM_TRACE
#define MEM_OFCHK

#ifdef MACBUS_USB
#define ATCMD_UART_DEV HG_UART0_DEVID
#else
#define ATCMD_UART_DEV HG_UART1_DEVID
#endif

#define UARTBUS_DEV    HG_UART0_DEVID
#define UARTBUS_DEV_BAUDRATE 115200

//#define K_900M
//#define K_860M
//#define K_810M

#define DEFAULT_SYS_CLK  64000000UL //options: 32M/48M/72M/144M, and 16*N from 64M to 128M

//#define FMAC_IOA
#define FMAC_IOB
#define FMAC_PAIR_KEY
//#define FMAC_ROLE_KEY
//#define FMAC_RSSI_LED
//#define FMAC_CONN_LED
//#define LED_INIT_BLINK 1

#define DEEP_SLEEP
//#define DC_DC_1_3V 1

//#define UART_TX_PA31 //用PA31做打印，PA13做uart-rx；



#endif
