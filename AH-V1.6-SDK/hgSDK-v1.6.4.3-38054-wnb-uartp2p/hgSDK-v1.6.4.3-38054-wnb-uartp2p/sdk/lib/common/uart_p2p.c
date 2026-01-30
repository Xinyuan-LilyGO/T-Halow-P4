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
#include "hal/gpio.h"
#include "hal/watchdog.h"
#include "hal/sysaes.h"
#include "hal/uart.h"
#include "hal/timer_device.h"
#include "hal/dma.h"
#include "lib/common.h"
#include "lib/syscfg.h"
#include "lib/skb/skb.h"
#include "lib/skb/skb_list.h"
#include "lib/mac_bus.h"
#include "lib/lmac/lmac.h"
#include "lib/lmac/lmac_host.h"
#include "lib/wnb/libwnb.h"
#include "lib/atcmd/libatcmd.h"

#ifdef UART_P2P_DEV
#define UARTP2P_RBSIZE (2048)
struct uart_p2p {
    struct os_semaphore sema;
	struct skb_list rx_data;
    RBUFFER_IDEF(rb, uint8, UARTP2P_RBSIZE);
    struct os_task task;
    struct dma_device *dma;
    struct uart_device *uart;
    struct timer_device *timer;
    uint8 txbuf[1500];
    struct hgic_tx_info txinfo;
    uint32 last_comm_time;
} uartp2p;

int32 wnb_uart_p2p_send_ether_data(struct hgic_tx_info *txinfo, uint8 *data, int32 len);

static void uart_p2p_tx_data(void)
{
    int32 count = 0;
    uint8 dest[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	//TX
	while (count < 1500 && !RB_EMPTY(&uartp2p.rb)) {
		RB_GET(&uartp2p.rb, uartp2p.txbuf[count]);
		count++;
	}
	if (count > 0) {
		if (0) {
			wnb_send_customer_data(&uartp2p.txinfo, dest, uartp2p.txbuf, count);	//mgmt frame
		} else {
			wnb_uart_p2p_send_ether_data(&uartp2p.txinfo, uartp2p.txbuf, count);	//data frame
		}
        uartp2p.last_comm_time = os_jiffies();
        gpio_set_val(WNB_P2P_SIGNAL_LED, 0);
	}
}

static void uart_p2p_rx_data(void)
{
	struct sk_buff *skb;

	//RX
	skb = skb_list_dequeue(&uartp2p.rx_data);
	if (skb) {
		if (uartp2p.uart) {
			uart_puts(uartp2p.uart, skb->data, skb->len);
            uartp2p.last_comm_time = os_jiffies();
            gpio_set_val(WNB_P2P_SIGNAL_LED, 0);
		}
		kfree_skb(skb);
	}
}

static void uart_p2p_task(void *args)
{
    while (1) {
        if (TIME_AFTER(os_jiffies(), uartp2p.last_comm_time + os_msecs_to_jiffies(30))) {
            if (!gpio_get_val(WNB_P2P_SIGNAL_LED)) {
                gpio_set_val(WNB_P2P_SIGNAL_LED, 1);
            }
        }
        os_sema_down(&uartp2p.sema, 10);
		uart_p2p_rx_data();
		uart_p2p_tx_data();
    }
}

static void uart_p2p_timer_cb(uint32 args)
{
    os_sema_up(&uartp2p.sema);
}

static int32 uart_p2p_irq_hdl(uint32 irq_flag, uint32 param1, uint32 param2)
{
    int32 ret = 0;

    if(uartp2p.uart == NULL){
        return 0;
    }

    switch (irq_flag) {
        case UART_INTR_RX_DATA_AVAIL:
            timer_device_stop(uartp2p.timer);
            if (RB_COUNT(&uartp2p.rb) > (UARTP2P_RBSIZE - 32)) {
                os_sema_up(&uartp2p.sema);
            }
            RB_SET(&uartp2p.rb, (uint8)param2);
            timer_device_start(uartp2p.timer, P2P_TIMER_MS*1000, uart_p2p_timer_cb, (uint32)&uartp2p);
            break;
        default:
            break;
    }
    return ret;
}

__init void uart_p2p_init(void)
{
    struct uart_device *uart;
	wnb_module_uart_p2p_init();
    RB_INIT(&uartp2p.rb, UARTP2P_RBSIZE);
    os_sema_init(&uartp2p.sema, 0);
    uartp2p.timer = (struct timer_device *)dev_get(UART_P2P_TIMER);
    ASSERT(uartp2p.timer);
    timer_device_open(uartp2p.timer, TIMER_TYPE_ONCE, 0);
    uart = (struct uart_device *)dev_get(UART_P2P_DEV);
    ASSERT(uart);
    uartp2p.dma = (struct dma_device *)dev_get(HG_DMAC_DEVID);
    ASSERT(uartp2p.dma);
    uart_ioctl(uart, UART_IOCTL_CMD_USE_DMA, (uint32)uartp2p.dma);
    uart_open(uart);
    uart_baudrate(uart, UART_P2P_BAUDRATE);
    uart_request_irq(uart, uart_p2p_irq_hdl, 0x81, 0);
    os_memset(&uartp2p.txinfo, 0, sizeof(struct hgic_tx_info));
    uartp2p.txinfo.tx_flags2 = HGIC_HDR_FLAGS2_MGMT_ACK;
    gpio_set_dir(WNB_P2P_SIGNAL_LED, GPIO_DIR_OUTPUT);
    OS_TASK_INIT("uartp2p", &uartp2p.task, uart_p2p_task, 0, OS_TASK_PRIORITY_HIGH, 512);
    uartp2p.uart = uart;
	skb_list_init(&uartp2p.rx_data);
}

void uart_p2p_recv(uint8 *data, uint32 len)
{
	struct sk_buff *skb;

	skb = alloc_skb(len);
	if (skb) {
        os_memcpy(skb->data, data, len); 	//copy data
        skb_put(skb, len);
        skb_list_queue(&uartp2p.rx_data, skb);
		os_sema_up(&uartp2p.sema);
    }
}

#endif

