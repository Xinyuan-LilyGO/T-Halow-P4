#include "typesdef.h"
#include "list.h"
#include "dev.h"
#include "osal/string.h"
#include "osal/semaphore.h"
#include "osal/mutex.h"
#include "osal/task.h"
#include "osal/timer.h"
#include "hal/gpio.h"
#include "lib/sysvar.h"
#include "lib/lmac/lmac.h"
#include "lib/wnb/libwnb.h"
#include "lib/ticker_api.h"
#include "lib/mcu/mcu_util.h"

#if (MCU_FUNCTION == 1)
#define HOST_POWER_ON_IO        PB_0
#define POWER_OFF_DETECT_IO     PB_3
#define POWER_HOLD_ON_IO        PB_2
#define MCU_PIR_DETECT_IO       PB_4 // detect in dsleep
#define MCU_PIR_SENSOR_L_IO     PA_2
#define MCU_PIR_SENSOR_H_IO     PA_3
#define MCU_PIR_WAKEUP_IO       PB_7

#define MCU_POR_WAKEUP_EDGE_RISE    0
#define MCU_POR_WAKEUP_EDGE_FALL    1

extern int32 cw2015_init(uint8 alert_thd);
extern int32 cw2015_get_voltage(uint16 *vcell, uint16 *rtt, uint16 *soc);

struct mcu_dev {
    struct os_task power_task;
    struct os_task battery_task;
    uint8  pir_sensor;
    uint8  bat_soc;
};

static struct mcu_dev mcu;

static void mcu_power_control_task(void *args)
{
    int32 new_val, last_val, defval;
    int8  power_off = 0;
    uint32 power_off_tick = 0;

    gpio_set_dir(POWER_HOLD_ON_IO, GPIO_DIR_OUTPUT);
    gpio_set_val(POWER_HOLD_ON_IO, 1);
    gpio_set_dir(HOST_POWER_ON_IO, GPIO_DIR_OUTPUT);
    gpio_set_val(HOST_POWER_ON_IO, 1);
    gpio_set_dir(POWER_OFF_DETECT_IO, GPIO_DIR_INPUT);
    mcu_pir_sensor_set(3);
    
    defval = 1;
    last_val = gpio_get_val(POWER_OFF_DETECT_IO);

    while (1) {
        os_sleep_ms(100);
        new_val = gpio_get_val(POWER_OFF_DETECT_IO);
        if (new_val != last_val) {
            if (new_val == defval) {
                os_printf("up\r\n");
                power_off = 0; 
            } else {
                os_printf("down\r\n");
                power_off = 1;
                power_off_tick = os_jiffies();
            }
            last_val = new_val;
        }
        if (power_off && DIFF_JIFFIES(power_off_tick, os_jiffies()) > 5000) {
            os_printf("power off\r\n");
            //gpio_set_dir(POWER_HOLD_ON_IO, GPIO_DIR_INPUT);
            gpio_set_val(HOST_POWER_ON_IO, 0);
            gpio_set_val(POWER_HOLD_ON_IO, 0);
            os_sleep(1);
        }
    }
}

int32 mcu_battery_soc_respone(struct wireless_nb *wnb, uint8 battery_soc)
{
    // 客户自行生成以太网数据发给服务器
    struct sk_buff *skb = NULL;
    uint8 alert_msg[] = "something data";
    skb = alloc_tx_skb(sizeof(alert_msg) + 256);
    if (skb) {
        skb_reserve(skb, 256);
        hw_memcpy(skb->data, alert_msg, sizeof(alert_msg));
        skb_put(skb, sizeof(alert_msg));
        wnb_macbus_send_data(wnb, alert_msg, sizeof(alert_msg));
    }
    return RET_OK;
}

#define ALERT_BY_AP
// 除了警报主动发送电量外，用户还可以在AP端调用int32 mcu_battery_soc_request(struct wireless_nb *wnb)查询电量
static int32 mcu_battery_alert(struct wireless_nb *wnb)
{
    int32 ret = RET_ERR;
#ifdef ALERT_BY_AP
    uint8 bat_soc = mcu_battery_soc_get();
    struct wnb_mgmt *mgmt;
    struct wnb_mgmt_tx tx = {
        .len    = sizeof(struct wnb_mgmt) + 1,
        .flags  = 0,
        .txinfo = 0,
    };
    uint8 *ptr = os_malloc(sizeof(struct wnb_mgmt) + 1);
    if (ptr) {
        mgmt = (struct wnb_mgmt *)ptr;
        mgmt->type = WNB_STYPE_BATTERY_SOC;
        mgmt->resp = 1;
        mgmt->cookie = wnb_cookie(wnb);
        mgmt->center_freq = wnb->curr_freq;
        tx.dest = wnb->stas[0].sta->addr;
        tx.mgmt = mgmt;
        os_memcpy(ptr + sizeof(struct wnb_mgmt), &bat_soc, 1);
        ret = wnb_send_mgmt(wnb, &tx);
        os_free(ptr);
    }
#else
    // 客户自行生成以太网数据发给服务器
    struct sk_buff *skb = NULL;
    uint8 alert_msg[] = "something data";
    skb = alloc_tx_skb(sizeof(alert_msg) + 256 + 32); // 32 bytes prepare for encrypt
    if (skb) {
        skb_reserve(skb, 256);
        hw_memcpy(skb->data, alert_msg, sizeof(alert_msg));
        skb_put(skb, sizeof(alert_msg));
        skb_list_queue(&wnb->datalist, skb);
        WNB_RUN(wnb);
        ret = RET_OK;
    }
#endif
    return ret;
}

static void mcu_battery_detect_task(void *args)
{
    struct wireless_nb *wnb = (struct wireless_nb *)args;
    uint8 alert_thd = 0;
    uint16 vcell, rtt, soc;
    
    if (cw2015_init(alert_thd) == RET_ERR) {
        // error handle
    }
    while(1) {
        if (cw2015_get_voltage(&vcell, &rtt, &soc) == RET_OK) {
            mcu.bat_soc = soc;
            if (soc <= alert_thd) {
                // 电池没电，关闭主控节省电源
                gpio_set_val(HOST_POWER_ON_IO, 0);
                // 用户自行构造alert_msg数组内容
                mcu_battery_alert(wnb);
            }
        } else {
            mcu.bat_soc = -1;
        }
        os_sleep(5);
    }
}

int32 mcu_pir_sensor_set(uint8 level)
{
    if (level > 3) {
        level = 3;
    }
    mcu.pir_sensor = level;
    gpio_set_dir(MCU_PIR_SENSOR_L_IO, GPIO_DIR_OUTPUT);
    gpio_set_dir(MCU_PIR_SENSOR_H_IO, GPIO_DIR_OUTPUT);
    gpio_set_val(MCU_PIR_SENSOR_L_IO, !!(level & BIT(0)));
    gpio_set_val(MCU_PIR_SENSOR_H_IO, !!(level & BIT(1)));
    return RET_OK;
}

uint8 mcu_pir_sensor_get(void)
{
    return mcu.pir_sensor;
}

uint8 mcu_battery_soc_get(void)
{
    return mcu.bat_soc;
}
#endif

int32 mcu_init(void)
{
    struct wireless_nb *wnb = sysvar(SYSVAR_ID_WIRELESS_NB);
    
    if (wnb->mcu_func) {
#if (MCU_FUNCTION == 1)
        OS_TASK_INIT("power_key", &mcu.power_task, mcu_power_control_task, 0, OS_TASK_PRIORITY_NORMAL, 512);
        OS_TASK_INIT("battery", &mcu.battery_task, mcu_battery_detect_task, wnb, OS_TASK_PRIORITY_NORMAL, 512);
        // 主控如果不设置，需要此处设置唤醒IO
        lmac_set_wakeup_io(wnb->ops, MCU_PIR_WAKEUP_IO, MCU_POR_WAKEUP_EDGE_RISE);
        lmac_set_wksrc_detect(wnb->ops, MCU_PIR_DETECT_IO, 1);
#endif
    }
    return RET_OK;
}