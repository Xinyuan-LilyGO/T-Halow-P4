// @file    uart_bus.c
// @author
// @brief

// Revision History
// V1.0.0  06/28/2019  First Release
// V1.0.1  08/06/2019  use uart1 when 40pin
// V1.0.2  10/28/2019  modify uart_bus_irq_hdl

#include "tx_platform.h"
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
#include "lib/skb/skb.h"
#include "lib/lmac/hgic.h"
#include "lib/mac_bus.h"
#include "lib/rbuffer.h"
#include "lib/drivercmd.h"
#include "lib/ticker_api.h"
#include "lib/syscfg.h"
#include "hal/uart.h"
#include "hal/gpio.h"
#include "hal/timer_device.h"
#include "hal/dma.h"
#include "osal/timer.h"
#include "lib/wnb/libwnb.h"
#include "lib/sysvar.h"

#define UART_BUS_CMD_SET_FIXLEN 1

#define UART_BUS_RX_BUF_SIZE (1600)
#define UART_BUS_RX_SKB_CNT  (4)
#if (WNB_FRM_TYPE == 2)
#define UART_RX_ADD (14)
#else
#define UART_RX_ADD (0)
#endif

struct mac_bus_uart {
    struct mac_bus bus;
    struct uart_device *uart;
    struct os_task task;
    struct os_semaphore sema;
    struct os_mutex lock;
    struct dma_device *dma;
    struct timer_device *timer;
    struct sk_buff *skb;
    uint8 *rxbuff;
    RBUFFER_IDEF(rskb, struct sk_buff *, UART_BUS_RX_SKB_CNT);
    RBUFFER_IDEF_R(rb, uint8, UART_BUS_RX_BUF_SIZE);
    uint32 rxcount;
    uint32 stop_pos;
    uint16 magic;
    uint16 frm_len;
    uint16 fixlen;
    uint8  rx_chn;
};

#ifdef MACBUS_UART
static void uart_bus_task(void *args)
{
    int32 i     = 0;
    int32 count = 0;
    struct sk_buff *skb = NULL;
    struct mac_bus_uart *uart_bus = (struct mac_bus_uart *)args;

    while (1) {
        os_sema_down(&uart_bus->sema, osWaitForever);
        if (uart_bus->fixlen > 0) {
            count = RB_COUNT(&uart_bus->rskb);
            if (count) {
                RB_GET(&uart_bus->rskb, skb);
                uart_bus->bus.recv(&uart_bus->bus, skb, uart_bus->fixlen);
            }
        } else {
            count = RB_COUNT(&uart_bus->rb);
            if (count > 0 && uart_bus->bus.recv) {
                skb = alloc_tx_skb(count + 256 + UART_RX_ADD + 32); // 32 bytes prepare for encrypt
                if (skb) {
                    i = 0;
                    skb_reserve(skb, 256 + UART_RX_ADD);
                    while (i < count && uart_bus->rb.rpos != uart_bus->stop_pos && !RB_EMPTY(&uart_bus->rb)) {
                        RB_GET(&uart_bus->rb, skb->data[i++]);
                    }
                    uart_bus->bus.recv(&uart_bus->bus, skb, i);
                }
            }
        }
    }
}

static void uart_bus_timer_cb(uint32 args)
{
    struct mac_bus_uart *uart_bus = (struct mac_bus_uart *)args;
    uart_bus->stop_pos = uart_bus->rb.wpos;
    uart_bus->rxcount = 0;
    uart_bus->frm_len = 0;
    uart_bus->magic   = 0;
    os_sema_up(&uart_bus->sema);
}

static int32 uart_bus_irq_hdl(uint32 irq_flag, uint32 param1, uint32 param2)
{
    struct mac_bus_uart *uart_bus = (struct mac_bus_uart *)param1;

    switch (irq_flag) {
        case UART_INTR_RX_DATA_AVAIL:
            if (uart_bus->fixlen > 0) {
                RB_SET(&uart_bus->rskb, uart_bus->skb);
                uart_bus->skb = alloc_tx_skb(uart_bus->fixlen + 256 + UART_RX_ADD + 32); // 32 bytes prepare for encrypt
                if (uart_bus->skb) {
                    skb_reserve(uart_bus->skb, 256 + UART_RX_ADD);
                    uart_bus->rx_chn = uart_gets(uart_bus->uart, uart_bus->skb->data, uart_bus->fixlen);
                }
                os_sema_up(&uart_bus->sema);
            } else {
                timer_device_stop(uart_bus->timer);
                RB_SET(&uart_bus->rb, (uint8)param2);
                uart_bus->rxcount++;
                if (!uart_bus->fixlen && uart_bus->rxcount <= 2) {
                    uart_bus->magic >>= 8;
                    uart_bus->magic |= (uint8)param2<<8;
                } else if (!uart_bus->fixlen && (uart_bus->rxcount == 5 || uart_bus->rxcount == 6)) {
                    uart_bus->frm_len >>= 8;
                    uart_bus->frm_len |= (uint8)param2<<8;
                }
                if ((uart_bus->fixlen && uart_bus->rxcount == uart_bus->fixlen) ||
                    (uart_bus->magic == 0x1A2B && uart_bus->frm_len && uart_bus->rxcount >= uart_bus->frm_len) ||
                    (RB_COUNT(&uart_bus->rb) >= UART_BUS_RX_BUF_SIZE - 1)) {
                    uart_bus->stop_pos = uart_bus->rb.wpos;
                    uart_bus->rxcount = 0;
                    uart_bus->frm_len = 0;
                    uart_bus->magic   = 0;
                    os_sema_up(&uart_bus->sema);
                }
                timer_device_start(uart_bus->timer, 5000, uart_bus_timer_cb, (uint32)uart_bus);
            }
            break;
        default:
            break;
    }
    return 0;
}

static int uart_bus_write(struct mac_bus *bus, uint8 *buff, int len)
{
    struct mac_bus_uart *uart_bus = container_of(bus, struct mac_bus_uart, bus);
    struct wireless_nb *wnb = (struct wireless_nb *)uart_bus->bus.priv;
    ASSERT(uart_bus->uart);
    //if (uart_bus->fixlen && len > uart_bus->fixlen) { return RET_ERR; }
    //uart_puts(uart_bus->uart, buff, uart_bus->fixlen ? uart_bus->fixlen : len);
    os_mutex_lock(&uart_bus->lock, osWaitForever);
    uart_puts(uart_bus->uart, buff, len);
    delay_us(10*24*1000000/wnb->cfg->uartbus_dev_baudrate); // 24Bytes
    os_mutex_unlock(&uart_bus->lock);
    return RET_OK;
}

static int32 uart_device_init(struct mac_bus_uart *uart_bus)
{
    uart_close(uart_bus->uart);
    delay_us(100); // very important!
    uart_open(uart_bus->uart);
    struct wireless_nb *wnb = (struct wireless_nb *)uart_bus->bus.priv;
    uart_baudrate(uart_bus->uart, wnb->cfg->uartbus_dev_baudrate);
    dma_stop(uart_bus->dma, uart_bus->rx_chn);
    os_free(uart_bus->skb);
    if (uart_bus->fixlen > 0) {
        os_free(uart_bus->rxbuff);
        uart_bus->skb = alloc_tx_skb(uart_bus->fixlen + 256 + UART_RX_ADD + 32); // 32 bytes prepare for encrypt
        if (uart_bus->skb) {
            skb_reserve(uart_bus->skb, 256 + UART_RX_ADD);
            uart_bus->rx_chn = uart_gets(uart_bus->uart, uart_bus->skb->data, uart_bus->fixlen);
        } else {
            return -ENOMEM;
        }
    } else {
        if (uart_bus->rxbuff == NULL) {
            uart_bus->rxbuff = os_malloc(UART_BUS_RX_BUF_SIZE+1);
        }
        if (uart_bus->rxbuff == NULL) {
            return -ENOMEM;
        }
        RB_INIT_R(&uart_bus->rb, UART_BUS_RX_BUF_SIZE, uart_bus->rxbuff);
    }
    uart_request_irq(uart_bus->uart, uart_bus_irq_hdl, 0x81, (uint32)uart_bus); // request irq at last
    return RET_OK;
}

static int32 uart_bus_set_fixlen(uint32 priv, struct driver_cmd_param *param)
{
    struct mac_bus_uart *uart_bus = (struct mac_bus_uart *)priv;
    uint16 fixlen = get_unaligned_le16(param->data);
    if (uart_bus->fixlen != fixlen) {
        sys_cfgs.uart_fixlen = fixlen;
        syscfg_save();
        uart_bus->fixlen = fixlen; // change fixlen after write flash
        uart_device_init(uart_bus);
    }
    os_printf("uart bus fixlen set %d\r\n", uart_bus->fixlen);
    return RET_OK;
}

static int32 uart_bus_cmd_hdl(uint32 priv, struct driver_cmd_param *param)
{
    struct mac_bus_uart *uart_bus = (struct mac_bus_uart *)priv;
    int32 ret = -MAX_ERRNO;
    uint32 cmd = param->cmd;

    switch (cmd) {
        case HGIC_CMD_SET_UART_FIXLEN:
            ret = uart_bus_set_fixlen(priv, param);
            break;
        case HGIC_CMD_GET_UART_FIXLEN:
            *param->resp = (uint8 *)os_malloc(2);
            if (*param->resp) {
                *param->resp_len = 2;
                put_unaligned_le16(uart_bus->fixlen, *param->resp);
                ret = 0;
            } else {
                ret = -ENOMEM;
            }
            break;
        default:
            break;
    }
    return ret;
}
#endif

/**
 * @brief UART mac bus initialization
 * 
 * @param recv  Mac bus recv function
 * @param priv  UART mac bus 
 * @return __init
 * 
 */
__init struct mac_bus *mac_bus_uart_attach(mac_bus_recv recv, void *priv)
{
    struct mac_bus_uart *uart_bus = NULL;
#ifdef MACBUS_UART
    int32 ret;

    uart_bus = os_zalloc(sizeof(struct mac_bus_uart));
    ASSERT(uart_bus && recv);
    uart_bus->bus.write = uart_bus_write;
    uart_bus->bus.type  = MAC_BUS_TYPE_UART;
    uart_bus->bus.recv  = recv;
    uart_bus->bus.priv  = priv;
#if MACBUS_UART_FIXLEN
    uart_bus->fixlen    = MACBUS_UART_FIXLEN;
#else
    uart_bus->fixlen    = (sys_cfgs.uart_fixlen > UART_BUS_RX_BUF_SIZE) ? UART_BUS_RX_BUF_SIZE : sys_cfgs.uart_fixlen;
#endif
    os_printf("uart bus fixlen=%d\r\n", uart_bus->fixlen);
    uart_bus->timer = (struct timer_device *)dev_get(UART_BUS_TIMER);
    ASSERT(uart_bus->timer);
    timer_device_open(uart_bus->timer, TIMER_TYPE_ONCE, 0);
    uart_bus->uart = (struct uart_device *)dev_get(UARTBUS_DEV);
    ASSERT(uart_bus->uart);
    uart_bus->dma = (struct dma_device *)dev_get(HG_DMAC_DEVID);
    ASSERT(uart_bus->dma);
    uart_ioctl(uart_bus->uart, UART_IOCTL_CMD_USE_DMA, (uint32)uart_bus->dma);
    RB_INIT(&uart_bus->rskb, UART_BUS_RX_SKB_CNT);
    os_sema_init(&uart_bus->sema, 0);
    os_mutex_init(&uart_bus->lock);
    OS_TASK_INIT("uart_bus", &uart_bus->task, uart_bus_task, uart_bus, OS_TASK_PRIORITY_HIGH, 512);
    ret = uart_device_init(uart_bus);
    ASSERT(!ret);
    ret = driver_cmd_hdl_register(uart_bus_cmd_hdl, (uint32)uart_bus);
    ASSERT(!ret);
#endif
    return &uart_bus->bus;
}

