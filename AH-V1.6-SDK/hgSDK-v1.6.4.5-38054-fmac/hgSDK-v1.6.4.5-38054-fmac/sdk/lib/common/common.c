#include "typesdef.h"
#include "errno.h"
#include "list.h"
#include "dev.h"
#include "osal/task.h"
#include "osal/sleep.h"
#include "osal/string.h"
#include "hal/dma.h"
#include "lib/common.h"

struct dma_device *m2mdma = NULL;

__weak void   system_sleepdata_reset(void){};
__weak void  *system_sleepdata_request(uint8 id, uint32 size) { return 0; }
__weak uint32 system_sleepdata_freesize(void) { return 0; }
// 任意模式下sta深度休眠或psmode5/6模式下任意角色休眠时执行hook
__at_section(".dsleep_text") __weak void system_sleep_enter_hook(void){};
// 任意模式下sta深度休眠周期唤醒时执行hook，用户利用额外参数和API控制接下来休眠或唤醒行为
__at_section(".dsleep_text") __weak void system_sleep_wakeup_hook(void){};
// 非psmode5/6模式下sta深度休眠唤醒周期内收到数据包执行hook，用户利用额外api控制唤醒行为
__at_section(".dsleep_text") __weak int32 system_sleep_rxdata_hook(uint8 *data, uint32 len)
{
    return 0;
}
// ap在5mA低功耗模式下通过io唤醒执行hook，用于判断多个唤醒源合并到一个io时做额外判断区分
__weak int32 system_ap_sleep_io_wakeup_hook(void)
{
    return 4;// DSLEEP_WK_REASON_IO 返回唤醒原因，默认返回IO唤醒，返回0不唤醒
}
// ap在任意低功耗模式下收到sta数据包或null包执行hook，用于唤醒时执行额外操作
__weak int32 system_ap_sleep_rxdata_hook(uint8 *data, uint32 len)
{
    return 1; // 0:ignore, 1: wakeup
}
// ap在任意低功耗模式下周期唤醒执行hook，用于周期唤醒做电源或其他额外检查
__weak void system_ap_sleep_wakeup_hook(void){};
// wakeup host前执行hook，用于客户根据唤醒原因检查和逻辑处理控制是否唤醒主控
__weak int32 system_wakeup_host_hook(uint8 wk_reason)
{
    return 1; // 0:skip wakeup host, 1:wakeup host
}

void cpu_loading_print(uint8 all)
{
    uint32 i = 0;
    uint32 diff_tick = 0;
    uint32 count = 32;
    struct os_task_time tsk_runtime[OS_TASK_COUNT + 1];
    static uint32 cpu_loading_tick = 0;
    uint32 jiff = os_jiffies();

    count = os_task_runtime(tsk_runtime, OS_TASK_COUNT + 1);
    diff_tick = DIFF_JIFFIES(cpu_loading_tick, jiff);
    cpu_loading_tick = jiff;

    os_printf("-----------------------------------------------\r\n");
    os_printf("Task Runtime Statistic, interval:%dms\r\n", diff_tick);
    os_printf("-----------------------------------------------\r\n");
    for (i = 0; i < count && i < OS_TASK_COUNT + 1; i++) {
        if (tsk_runtime[i].time > 0 || all) {
            os_printf("%2d  %-16s %2d%% (%5d) (%p)\r\n",
                      tsk_runtime[i].id,
                      tsk_runtime[i].name ? tsk_runtime[i].name : (const uint8 *)"----",
                      (tsk_runtime[i].time * 100) / diff_tick,
                      tsk_runtime[i].time, tsk_runtime[i].arg);
        }
    }
    os_printf("-----------------------------------------------\r\n");
}

int strncasecmp(const char *s1, const char *s2, int n)
{
    size_t i = 0;
    
    for (i = 0; i < n && s1[i] && s2[i]; i++) {
        if (s1[i] == s2[i] || s1[i] + 32 == s2[i] || s1[i] - 32 == s2[i]) {
        } else {
            break;
        }
    }
    return (i != n);
}

int strcasecmp(const char *s1, const char *s2)
{
    while (*s1 || *s2) {
        if (*s1 == *s2 || *s1 + 32 == *s2 || *s1 - 32 == *s2) {
            s1++; s2++;
        } else {
            return -1;
        }
    }
    return 0;
}

void hw_memcpy(void *dest, const void *src, uint32 size)
{
    if (dest && src) {
        if (m2mdma && size > 45) {
            dma_memcpy(m2mdma, dest, src, size);
        } else {
            os_memcpy(dest, src, size);
        }
    }
}

void hw_memset(void *dest, uint8 val, uint32 n)
{
    if (dest) {
        if (m2mdma && n > 12) {
            dma_memset(m2mdma, dest, val, n);
        } else {
            os_memset(dest, val, n);
        }
    }
}

void sdk_version(uint32 *sdk_ver, uint32 *svn_ver, uint32 *app_ver)
{
    *sdk_ver = SDK_VERSION;
    *svn_ver = SVN_VERSION;
    *app_ver = APP_VERSION;
}

__weak int32 customer_driver_data_proc(uint8 *data, uint32 len)
{
    return -ESRCH;
}
