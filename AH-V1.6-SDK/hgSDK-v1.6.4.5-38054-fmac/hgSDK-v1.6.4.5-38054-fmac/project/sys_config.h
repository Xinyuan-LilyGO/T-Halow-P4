#ifndef __SYS_CONFIG_H__
#define __SYS_CONFIG_H__
#include <stdint.h>
#include "project_config.h"


#define RTOS_ENABLE
#define OS_TASK_COUNT 20

#define PROJECT_TYPE PRO_TYPE_FMAC

#define SYS_KEY_OPT_EN                  0

#define USING_TX_SQ
#define TDMA_DEBUG
#define RXSUBFRAME_EN       1
#define RXSUBFRAME_PERIOD   (5)

#ifndef TDMA_BUFF_SIZE
#define TDMA_BUFF_SIZE    (32*4*100)
#endif
#ifndef SYS_HEAP_SIZE
#define  SYS_HEAP_SIZE (34*1024+1024*WNB_STA_COUNT+1024*WNB_STA_COUNT)
#endif
#ifndef WIFI_RX_BUFF_SIZE
#if (128*1024) >= (1024*WNB_STA_COUNT)
#define  WIFI_RX_BUFF_SIZE (128*1024-1024*WNB_STA_COUNT)
#else
#define  WIFI_RX_BUFF_SIZE (0)
#endif
#endif
#if WIFI_RX_BUFF_SIZE < (64*1024)
#undef WIFI_RX_BUFF_SIZE
#define WIFI_RX_BUFF_SIZE (64*1024)
#endif

#ifdef MACBUS_SDIO
#define SRAM_TAIL_SIZE (4096)
#else
#define SRAM_TAIL_SIZE (0)
#endif

extern uint32_t userpool_start;
extern uint32_t userpool_end;
#define USER_POOL_START   (userpool_start)
#define USER_POOL_SIZE    ((userpool_end) - (userpool_start) - SRAM_TAIL_SIZE)
#define TDMA_BUFF_ADDR    (USER_POOL_START)
#define SYS_HEAP_START    (TDMA_BUFF_ADDR+TDMA_BUFF_SIZE)
#define WIFI_RX_BUFF_ADDR (SYS_HEAP_START+SYS_HEAP_SIZE)
#define SKB_POOL_ADDR     (WIFI_RX_BUFF_ADDR+WIFI_RX_BUFF_SIZE)
#define SKB_POOL_SIZE     (USER_POOL_START+USER_POOL_SIZE-SKB_POOL_ADDR)

#define K_EXT_PA
//#define K_iPA
#ifdef K_EXT_PA
#define K_VMODE_CNTL
#endif

#define K_SWITCH_PWR_PA22

#ifndef WNB_IFBUS_TYPE
#ifdef MACBUS_SDIO
#define WNB_IFBUS_TYPE MAC_BUS_TYPE_SDIO
#elif defined(MACBUS_USB)
#define WNB_IFBUS_TYPE MAC_BUS_TYPE_USB
#elif defined(MACBUS_UART)
#define WNB_IFBUS_TYPE MAC_BUS_TYPE_UART
#endif
#endif

#ifndef LED_INIT_BLINK
#define LED_INIT_BLINK 0
#endif

#ifndef WNB_SUPP_PWR_OFF
#define WNB_SUPP_PWR_OFF 0
#endif

#ifndef WNB_TX_POWER
#define WNB_TX_POWER 20
#endif

#ifndef DSLEEP_PAPWRCTL_DIS
#define DSLEEP_PAPWRCTL_DIS 0
#endif

#ifndef DC_DC_1_3V
#define DC_DC_1_3V 0
#endif

#ifndef FMAC_WKIO_MODE
#define FMAC_WKIO_MODE 0
#endif

#ifndef MCU_FUNCTION
#define MCU_FUNCTION 0
#else
#define MCU_PIR_SENSOR_MUX_SPI_IO
#endif

#ifndef FMAC_AUTO_SAVE
#define FMAC_AUTO_SAVE 1
#endif

#ifndef FMAC_PS_MODE
#define FMAC_PS_MODE 0
#endif

#ifndef FMAC_BSS_BW
#define FMAC_BSS_BW 8
#endif

#ifndef MACBUS_UART_FIXLEN
#define MACBUS_UART_FIXLEN 0
#endif

#ifndef FMAC_BEACON_INT
#define FMAC_BEACON_INT 500
#endif

#ifndef FMAC_HEARTBEAT_INT
#define FMAC_HEARTBEAT_INT 500
#endif

#ifndef WNB_NEW_KEY
#define WNB_NEW_KEY 0
#endif

#ifndef AUTO_SLEEP_TIME
#define AUTO_SLEEP_TIME 10
#endif

//when using sleep，choose mcs0，else choose mcs10
#ifndef BW1M_MGMT_MCS
#define BW1M_MGMT_MCS MCS0
#endif

#ifndef TRV_PILOT
#define TRV_PILOT 1
#endif

#define K_SINGLE_PIN_SWITCH
//#define K_MIPI_IF

#define RF_PARA_FROM_NOR   //if open it to read parameter from nor in case efuse is not ready
                           //when efuse data is ready, close it

#define HGGPIO_DEFAULT_LEVEL 2

#define US_TICKER_TIMER HG_TIMER0_DEVID
#define UART_BUS_TIMER  HG_TIMER2_DEVID

#ifndef ERRLOG_SIZE
#define ERRLOG_SIZE (4096)
#endif

#define UART_P2P_TIMER  HG_TIMER2_DEVID


//#define DUAL_ANT_OPT


#define MEM_DMA_ENABLE
struct sys_config {
    uint16_t magic_num, crc;
    uint16_t size, rev1, rev2, rev3;
    uint8_t  wnbcfg[512+40*WNB_STA_COUNT];
    uint16_t uart_fixlen;
};
extern struct sys_config sys_cfgs;

#endif
