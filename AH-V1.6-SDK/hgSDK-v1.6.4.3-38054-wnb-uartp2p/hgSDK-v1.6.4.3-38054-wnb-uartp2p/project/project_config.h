#ifndef __SYS_PROJECT_CONFIG_H__
#define __SYS_PROJECT_CONFIG_H__
#define WNB_STA_COUNT (8)
//#define WNB_HEARTBEAT_INT 2000

#define AUTO_ETHERNET_PHY
//#define ETHERNET_IP101G
//#define ETHERNET_SZ18201
#define ETHERNET_PHY_ADDR               -1 //adaptive addr

#define DEFAULT_SYS_CLK    96000000UL //options: 32M/48M/72M/144M, and 16*N from 64M to 128M
#define ATCMD_UARTDEV      HG_UART1_DEVID

#define USING_ROLE_LED
//#define IGONOR_ROLE_KEY

#define DIS_PRINT
//#define DUAL_UART_DBG_PRINT  //Dbg output log-uart1-p2p TX-PA31
#define P2P_TIMER_MS 2
#define UART_P2P_DEV      HG_UART1_DEVID
#define UART_P2P_BAUDRATE 115200

//#define USE_915M_UPPER

#define WNB_MAC_SSID

#define WNB_P2P_SIGNAL_LED  255//PA_9


#endif
