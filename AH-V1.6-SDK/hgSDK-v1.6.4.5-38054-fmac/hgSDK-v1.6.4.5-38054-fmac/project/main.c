#include "typesdef.h"
#include "list.h"
#include "dev.h"
#include "devid.h"
#include "osal/string.h"
#include "osal/semaphore.h"
#include "osal/mutex.h"
#include "osal/irq.h"
#include "osal/task.h"
#include "osal/sleep.h"
#include "osal/timer.h"
#include "osal/string.h"
#include "hal/gpio.h"
#include "hal/watchdog.h"
#include "lib/common.h"
#include "lib/syscfg.h"
#include "lib/sysheap.h"
#include "lib/skb/skb.h"
#include "lib/skb/skb_list.h"
#include "lib/mac_bus.h"
#include "lib/lmac/lmac.h"
#include "lib/lmac/lmac_host.h"
#include "lib/wnb/libwnb.h"
#include "lib/atcmd/libatcmd.h"
#include "lib/xmodem.h"
#include "lib/mcu/mcu_util.h"

#ifndef LED_LEVEL_INVERT
#define LED_ON  0
#define LED_OFF 1
#else
#define LED_ON  1
#define LED_OFF 0
#endif

#if defined(MACBUS_SDIO) || defined(FMAC_IOB)
#define WNB_PAIR_KEY      PB_1
//#define WNB_PAIR_LED      PB_3
#define WNB_PAIR_LED      PB_4
#define WNB_SIGNAL_LED1   PB_5
#define WNB_SIGNAL_LED2   PB_6
#define WNB_SIGNAL_LED3   PB_7
//#define WNB_CONN_LED_IO   PB_3
#define WNB_CONN_LED_IO   PB_4
#define WNB_ROLE_KEY      PB_2
#define DISABLE_JTAG()
#else
#define WNB_PAIR_KEY      PA_7
#define WNB_PAIR_LED      PA_6
#define WNB_SIGNAL_LED1   PA_9
#define WNB_SIGNAL_LED2   PA_31
#define WNB_SIGNAL_LED3   PA_30
#define WNB_CONN_LED_IO   PA_6
#define WNB_ROLE_KEY      PA_8
#define DISABLE_JTAG()    jtag_map_set(0)
#endif

#ifdef FMAC_PAIR_KEY
static struct os_task wnb_pairkey_task;
#endif
#ifdef FMAC_ROLE_KEY
static struct os_task wnb_rolekey_task;
#endif

struct wnb_config wnbcfg = {
    .role = WNB_STA_ROLE_SLAVE,
    .chan_list = {9080, 9160, 9240},
    .chan_cnt = 3,
    .bss_bw   = FMAC_BSS_BW,
    .pri_chan = 3,
    .tx_mcs   = 0xff,
    .encrypt  = 1,
    .forward  = 1,
    .max_sta  = WNB_STA_COUNT,
    .acs_enable = 1,
    .bss_max_idle = 300,
    .beacon_int = FMAC_BEACON_INT,
    .heartbeat_int = FMAC_HEARTBEAT_INT,
    .dtim_period = 2,
    .wkio_mode   = FMAC_WKIO_MODE,
    .pa_pwrctrl_dis = DSLEEP_PAPWRCTL_DIS,
    .dcdc13 = DC_DC_1_3V,
    .not_auto_save = !FMAC_AUTO_SAVE,
    .psmode = FMAC_PS_MODE,
    .dupfilter = 1,
    .supper_pwr_set = WNB_SUPP_PWR_OFF,
    .tx_power = WNB_TX_POWER,
    .uartbus_dev_baudrate = UARTBUS_DEV_BAUDRATE,
};

static uint8 sysdbg_heap = 0;
static uint8 sysdbg_top = 0;
static uint8 sysdbg_irq = 0;
static uint8 sysdbg_wnb = 0;
static uint8 sysdbg_lmac = 2;
static uint8 pair_waitting = 0;
#if ERRLOG_SIZE
static int32 sys_atcmd_errlog_hdl(const char *cmd, uint8 *data, int32 len)
{
    char *arg = atcmd_args(0);
    if(atcmd_args_num() == 1){
        errlog_action(os_atoi(arg));
    }
    return 0;
}
#endif

static int32 sys_atcmd_sysdbg_hdl(const char *cmd, uint8 *data, int32 len)
{
    char *arg = atcmd_args(0);
    if (atcmd_args_num() == 2) {
        if (os_strcasecmp(arg, "heap") == 0) {
            sysdbg_heap = os_atoi(atcmd_args(1));
        }
        if (os_strcasecmp(arg, "top") == 0) {
            sysdbg_top = os_atoi(atcmd_args(1));
        }
        if (os_strcasecmp(arg, "irq") == 0) {
            sysdbg_irq = os_atoi(atcmd_args(1));
        }
        if (os_strcasecmp(arg, "wnb") == 0) {
            sysdbg_wnb = os_atoi(atcmd_args(1));
        }
        if (os_strcasecmp(arg, "lmac") == 0) {
            sysdbg_lmac = os_atoi(atcmd_args(1));
        }
        atcmd_ok;
    } else {
        atcmd_error;
    }
    return 0;
}

static int32 syscfg_save_def(void)
{
    int ret = RET_ERR;
    if (sys_cfgs.magic_num != SYSCFG_MAGIC) {
        // set syscfg default value
        sys_cfgs.uart_fixlen = 0;
        // save wnbcfg (actully total syscfg) at last
        ret = wnb_save_cfg(&wnbcfg, 1);
        os_printf("save default syscfg %s\r\n", ret ? "Fail" : "OK");
    }
    return ret;
}

#ifdef FMAC_PAIR_KEY
static int32 wnb_pair_key_val(void)
{
    int32 i, v;
    int32 v0 = 0, v1 = 0;

    for (i = 0; i < 50; i++) {
        v = gpio_get_val(WNB_PAIR_KEY);
        if (v) { v1++; }
        else  { v0++; }
    }
    return (v1 > v0 ? 1 : 0);
}

static void wnb_pair_key_task(void *args)
{
    int32 new_val, last_val, defval;

    defval  = 1;
    last_val = wnb_pair_key_val();
    while (1) {
        os_sleep_ms(100);
        new_val = wnb_pair_key_val();
        if (new_val != last_val) {
            if (new_val == defval) {
                wnb_pairing(0);
            } else {
                wnb_pairing(1);
            }
            last_val = new_val;
        }
    }
}
#endif

#ifdef FMAC_ROLE_KEY
static void wnb_role_key_task(void *args)
{
    int32 new_val, last_val;
    last_val = gpio_get_val(WNB_ROLE_KEY);
    while (1) {
        os_sleep_ms(100);
        new_val = gpio_get_val(WNB_ROLE_KEY);
        if (new_val != last_val) {
            os_printf("role key: reset\r\n");
            os_sleep_ms(100);
            mcu_reset();
        }
    }
}
#endif

#ifdef USING_STA_LED
static void wnb_sta_led(uint8 sta_num)
{
    gpio_set_val(WNB_CONN_LED_IO, sta_num >= 1 ? LED_ON : LED_OFF);
    gpio_set_val(WNB_SIGNAL_LED1, sta_num >= 2 ? LED_ON : LED_OFF);
    gpio_set_val(WNB_SIGNAL_LED2, sta_num >= 3 ? LED_ON : LED_OFF);
    gpio_set_val(WNB_SIGNAL_LED3, sta_num >= 4 ? LED_ON : LED_OFF);
}
#endif

static void wnb_event_hdl(wnb_event evt, uint32 param1, uint32 param2)
{
    static int __pair_led_on = 0;
    switch (evt) {
        case WNB_EVENT_PAIR_START:
            pair_waitting = 1;
            os_printf("wnb: start pairing ...\r\n");
#ifdef FMAC_CONN_LED
            gpio_set_val(WNB_PAIR_LED, LED_OFF);
#endif
            break;
        case WNB_EVENT_PAIR_SUCCESS:
#ifdef FMAC_CONN_LED
            pair_waitting = 0;
            gpio_set_val(WNB_PAIR_LED, __pair_led_on ? LED_ON : LED_OFF);
#endif
            __pair_led_on = !__pair_led_on;
            os_printf("wnb: pairing success!\r\n");
            break;
        case WNB_EVENT_PAIR_DONE:
            pair_waitting = 0;
#ifndef USING_STA_LED
#ifdef FMAC_CONN_LED
            gpio_set_val(WNB_PAIR_LED, LED_OFF);          //disable WNB_PAIR_LED
            if (param2 > 0) {
                gpio_set_val(WNB_CONN_LED_IO, LED_ON);   //disable WNB_CONN_LED_IO
            } else {
                gpio_set_val(WNB_CONN_LED_IO, LED_OFF);   //disable WNB_CONN_LED_IO
            }
#endif
#else
            wnb_sta_led(param2);
#endif
            os_printf("wnb: pairing done, save wnb config!\r\n");
            if (wnb_save_cfg((struct wnb_config *)param1, 0)) {
                os_printf("save wnb config fail, try again!\r\n");
                wnb_save_cfg((struct wnb_config *)param1, 0);
            }
            break;
        case WNB_EVENT_RX_DATA:
            os_printf("wnb: rx customer data 0x%x, len:%d\r\n", param1, param2);
            break;
        case WNB_EVENT_CONNECT_START:
            os_printf("wnb: start connecting ...\r\n");
            break;
        case WNB_EVENT_CONNECTED:
#ifndef USING_STA_LED
            os_printf("wnb: ["MACSTR"] connect success!\r\n", MAC2STR((uint8 *)param1));
#ifdef FMAC_CONN_LED
            gpio_set_val(WNB_CONN_LED_IO, LED_ON);
#endif
#else
            wnb_sta_led(param2);
#endif
            break;
        case WNB_EVENT_DISCONNECTED:
#ifndef USING_STA_LED
            os_printf("wnb: connection lost!\r\n");
            if (param2 == 0) {
#ifdef FMAC_CONN_LED
                gpio_set_val(WNB_CONN_LED_IO, LED_OFF);
#endif
#ifdef FMAC_RSSI_LED
                gpio_set_val(WNB_SIGNAL_LED3, LED_OFF);
                gpio_set_val(WNB_SIGNAL_LED2, LED_OFF);
                gpio_set_val(WNB_SIGNAL_LED1, LED_OFF);
#endif
            }
#else
            wnb_sta_led(param2);
#endif
            break;
        case WNB_EVENT_RSSI:/*param1: rssi, param2: sta count*/
#ifndef USING_STA_LED
#ifdef FMAC_RSSI_LED
            if (param2 == 0) {/*ap:no sta, or sta: disconnected*/
                gpio_set_val(WNB_SIGNAL_LED3, LED_OFF);
                gpio_set_val(WNB_SIGNAL_LED2, LED_OFF);
                gpio_set_val(WNB_SIGNAL_LED1, LED_OFF);
            } else if (param2 == 1) {/*AP: has 1 sta, or STA: connected to AP*/
                gpio_set_val(WNB_SIGNAL_LED3, ((int8)param1 >= -48) ? LED_ON : LED_OFF);
                gpio_set_val(WNB_SIGNAL_LED2, ((int8)param1 >= -60) ? LED_ON : LED_OFF);
                gpio_set_val(WNB_SIGNAL_LED1, ((int8)param1 >= -72) ? LED_ON : LED_OFF);
            } else { /*only for AP mode: more than 1 sta*/
                gpio_set_val(WNB_SIGNAL_LED3, LED_ON);
                gpio_set_val(WNB_SIGNAL_LED2, LED_ON);
                gpio_set_val(WNB_SIGNAL_LED1, LED_ON);
            }
#endif
#else
            wnb_sta_led(param2);
#endif
            break;
        case WNB_EVENT_EVM:
#ifndef USING_STA_LED
#ifdef FMAC_RSSI_LED
            if (param2 == 1) {
                if (((int8)param1) >= -20) {
                    gpio_set_val(WNB_SIGNAL_LED3, LED_OFF);
                }
                if (((int8)param1) >= -15) {
                    gpio_set_val(WNB_SIGNAL_LED2, LED_OFF);
                }
            }
#endif
#else
            wnb_sta_led(param2);
#endif
            break;
        default:
            break;
    }
}

static void led_blink(void)
{
#if LED_INIT_BLINK
    int32 i = 4;
    while (i-- > 0) {
        gpio_set_val(WNB_PAIR_LED, (i & 0x1));
        gpio_set_val(WNB_SIGNAL_LED1, (i & 0x1));
        gpio_set_val(WNB_SIGNAL_LED2, (i & 0x1));
        gpio_set_val(WNB_SIGNAL_LED3, (i & 0x1));
        os_sleep_ms(500);
    }
    gpio_set_val(WNB_PAIR_LED, LED_OFF);
    gpio_set_val(WNB_SIGNAL_LED1, LED_OFF);
    gpio_set_val(WNB_SIGNAL_LED2, LED_OFF);
    gpio_set_val(WNB_SIGNAL_LED3, LED_OFF);
#endif
}

static void sys_pair_waiting(void)
{
    static uint8 waiting_led = 0;
    if (pair_waitting) {
        gpio_set_val(WNB_PAIR_LED, waiting_led ? LED_ON : LED_OFF);
        waiting_led = !waiting_led;
    }
}

static void sys_atcmd_init(void)
{
    atcmd_init(ATCMD_UART_DEV);
    atcmd_register("AT+FWUPG", xmodem_fwupgrade_hdl, NULL);
    atcmd_register("AT+SYSDBG", sys_atcmd_sysdbg_hdl, NULL);
    atcmd_register("AT+SKBDUMP", atcmd_skbdump_hdl, NULL);
#if ERRLOG_SIZE
    atcmd_register("AT+ERRLOG", sys_atcmd_errlog_hdl, NULL);
#endif
}

static void sys_wifi_init(void)
{
    struct lmac_init_param lparam;
    struct wnb_init_param param;

#ifdef SKB_POOL_ENABLE
    skb_txpool_init((uint8 *)SKB_POOL_ADDR, (uint32)SKB_POOL_SIZE);
#endif

    os_memset(&param, 0, sizeof(param));
    os_memset(&lparam, 0, sizeof(lparam));
    #ifdef MACBUS_USB
        lparam.uart_tx_io = 1;//pa11
    #else
        #ifdef UART_TX_PA31
            lparam.uart_tx_io = 2;//pa31
        #else
            lparam.uart_tx_io = 0;//pa13
        #endif
    #endif 
    lparam.rxbuf = WIFI_RX_BUFF_ADDR;
    lparam.rxbuf_size = WIFI_RX_BUFF_SIZE;
    lparam.tdma_buff = TDMA_BUFF_ADDR;
    lparam.tdma_buff_size = TDMA_BUFF_SIZE;
    param.ops = lmac_ah_init(&lparam);
    if (module_efuse_info.module_type == MODULE_TYPE_750M) {
        wnbcfg.chan_list[0] = 7640;
        wnbcfg.chan_list[1] = 7720;
        wnbcfg.chan_list[2] = 7800;
    } else if (module_efuse_info.module_type == MODULE_TYPE_810M) {
        wnbcfg.chan_list[0] = 8060;
        wnbcfg.chan_list[1] = 8140;
        wnbcfg.chan_cnt = 2;
    } else if (module_efuse_info.module_type == MODULE_TYPE_850M) {
        wnbcfg.chan_list[0] = 8450;
        wnbcfg.chan_list[1] = 8550;
        wnbcfg.chan_cnt = 2;
    } else if (module_efuse_info.module_type == MODULE_TYPE_860M) {
        wnbcfg.chan_list[0] = 8660;
        wnbcfg.chan_cnt = 1;
        wnbcfg.bss_bw   = 2;
    } else { // 915M case
        ;
    }

    wnb_load_cfg(&wnbcfg);
    ASSERT(wnbcfg.max_sta == WNB_STA_COUNT);
    wnb_load_factparam(&wnbcfg);
    sysctrl_efuse_mac_addr_calc(wnbcfg.addr);

    param.cb  = wnb_event_hdl;
    param.cfg = &wnbcfg;
    param.frm_type = WNB_FRM_TYPE;
    param.if_type  = WNB_IFBUS_TYPE;
    param.hook_cnt = 10;
    wnb_init(&param);
    syscfg_save_def();
    wnb_connect(1);
}

static void sys_keyled_init(void)
{
#ifdef FMAC_RSSI_LED
    DISABLE_JTAG();
    gpio_set_dir(WNB_SIGNAL_LED1, GPIO_DIR_OUTPUT);
    gpio_set_dir(WNB_SIGNAL_LED2, GPIO_DIR_OUTPUT);
    gpio_set_dir(WNB_SIGNAL_LED3, GPIO_DIR_OUTPUT);
    gpio_set_val(WNB_SIGNAL_LED1, LED_OFF);
    gpio_set_val(WNB_SIGNAL_LED2, LED_OFF);
    gpio_set_val(WNB_SIGNAL_LED3, LED_OFF);
#endif

#ifdef FMAC_CONN_LED
    gpio_set_dir(WNB_PAIR_LED, GPIO_DIR_OUTPUT);
    gpio_set_val(WNB_PAIR_LED, LED_OFF);
#endif

    led_blink();

#ifdef FMAC_PAIR_KEY
    gpio_set_dir(WNB_PAIR_KEY, GPIO_DIR_INPUT);
    gpio_set_mode(WNB_PAIR_KEY, GPIO_PULL_UP, GPIO_PULL_LEVEL_100K);
    OS_TASK_INIT("pair_key", &wnb_pairkey_task, wnb_pair_key_task, 0, OS_TASK_PRIORITY_NORMAL, 512);
#endif

#ifdef FMAC_ROLE_KEY
    gpio_set_dir(WNB_ROLE_KEY, GPIO_DIR_INPUT);
    gpio_set_mode(WNB_ROLE_KEY, GPIO_PULL_DOWN, GPIO_PULL_LEVEL_100K);
    wnbcfg.role = gpio_get_val(WNB_ROLE_KEY) ? WNB_STA_ROLE_MASTER : WNB_STA_ROLE_SLAVE;
    OS_TASK_INIT("role_key", &wnb_rolekey_task, wnb_role_key_task, 0, OS_TASK_PRIORITY_NORMAL, 512);
#endif
}


int main(void)
{
    int8 print_interval  = 0;

    sys_monitor_reset();
    mcu_watchdog_timeout(5);
    syscfg_init(&sys_cfgs, sizeof(sys_cfgs));
    sys_atcmd_init();
    sys_wifi_init();
    sys_keyled_init();

    mcu_init();
    
#ifdef UART_P2P_DEV
void uart_p2p_init(void);
    uart_p2p_init();
#endif

#ifdef DIS_PRINT
    disable_print();
#endif

#ifdef GPIOB_1V8
    pmu_vccmipi_vol_set_sec(VCCMIPI_1P8V);
#endif

    sysheap_collect_init();
    while (1) {
        os_sleep(1);
        sys_pair_waiting();
        if (print_interval++ >= 5) {
            if (sysdbg_top) cpu_loading_print(0);
            if (sysdbg_heap) sysheap_status();
            if (sysdbg_irq) irq_status();
            if (sysdbg_wnb) wnb_statistic_print();
            lmac_transceive_statics(sysdbg_lmac);
            print_interval = 0;
        }
    }
}

