
#include "typesdef.h"
#include "list.h"
#include "dev.h"
#include "devid.h"
#include "osal/string.h"
#include "osal/mutex.h"
#include "osal/semaphore.h"
#include "osal/task.h"
#include "hal/sdio_slave.h"
#include "hal/gpio.h"
#include "lib/skb/skb.h"
#include "lib/mac_bus.h"
#include "lib/lmac/hgic.h"

struct mac_bus_sdio {
    struct mac_bus     bus;
    struct os_mutex    tx_lock;
    struct sdio_slave *dev;
    int32              ready;
    int32              part_len;
    uint8             *rx_buff;
    struct sk_buff    *cmdskb;
};

#define SDIO_FUNC_CIS_SIZE  (76)

static int32 sdio_bus_write(struct mac_bus *bus, uint8 *buff, int32 len)
{
    int32 ret = RET_OK;
    struct mac_bus_sdio *sdio_bus = container_of(bus, struct mac_bus_sdio, bus);

    if (!sdio_bus->ready) {
        sdio_bus->bus.txerr++;
        return RET_ERR;
    }
    ret = sdio_slave_write(sdio_bus->dev, buff, ALIGN(len, 4), TRUE);
    if (ret) {
        sdio_bus->bus.txerr++;
        sdio_bus->ready = 0;
    }
    return ret;
}

static int32 sdio_bus_ioctl(struct mac_bus *bus, uint32 param1, uint32 param2)
{
    int32 ret = RET_OK;
    struct mac_bus_sdio *sdio_bus = container_of(bus, struct mac_bus_sdio, bus);

    switch (param1) {
        case MAC_BUS_IOCTL_CMD_SUSPEND:
            ret = sdio_slave_ioctl(sdio_bus->dev, SDIO_SLAVE_IO_CMD_SET_RST_TIMER, 0);
            break;
        case MAC_BUS_IOCTL_CMD_RESUME:
            ret = sdio_slave_ioctl(sdio_bus->dev, SDIO_SLAVE_IO_CMD_SET_RST_TIMER, 1);
            break;
        default:
            break;
    }
    return ret;
}

static void sdio_bus_irq(uint32 irq, uint32 param1, uint32 param2, uint32 param3)
{
    struct mac_bus_sdio *sdio_bus = (struct mac_bus_sdio *)param1;
    struct hgic_hdr *hdr = (struct hgic_hdr *)sdio_bus->rx_buff;
    //gpio_set_val(1, 1);
    switch (irq) {
        case SDIO_SLAVE_IRQ_RX:
#ifdef SDIO_PART_TRANS
            if (sdio_bus->part_len == 0 && hdr->magic == HGIC_HDR_TX_MAGIC && param3 < hdr->length) {
                sdio_bus->part_len = param3;
                break;
            }
            if (sdio_bus->part_len > 0) param3 += sdio_bus->part_len;
            sdio_bus->part_len = 0;
#endif

            if(param3 > 0 && param3 < 1900){
                struct sk_buff *skb = alloc_tx_skb(1900);
                if(skb) skb_reserve(skb, 256);
                else if(hdr->magic == HGIC_HDR_TX_MAGIC && 
                       (hdr->type == HGIC_HDR_TYPE_CMD || hdr->type == HGIC_HDR_TYPE_CMD2 || hdr->type == HGIC_HDR_TYPE_BOOTDL)){
                    skb = sdio_bus->cmdskb;
                    sdio_bus->cmdskb = NULL;
                    os_printf("cmdskb: %p\r\n", skb);
                }

                if(skb){
                    hw_memcpy(skb->data, sdio_bus->rx_buff, param3);
                    sdio_bus->bus.recv(&sdio_bus->bus, skb, param3);
                }else{
                    sdio_bus->bus.rxerr++;
                }
            }else{
                sdio_bus->bus.rxerr++;
            }
            sdio_bus->rx_buff[0] = 0xFF;
            sdio_slave_read(sdio_bus->dev, sdio_bus->rx_buff, (2048-76), FALSE);
            sdio_bus->ready = 1;
            break;
        case SDIO_SLAVE_IRQ_RESET:            
            sdio_bus->rx_buff[0] = 0xFF;
            sdio_slave_read(sdio_bus->dev, sdio_bus->rx_buff, (2048-76), FALSE);
            break;
        default:
            break;
    }
    //gpio_set_val(1, 0);
}

static void sdio_bus_cmdskb_free(void *priv, struct sk_buff *skb)
{
    struct mac_bus_sdio *sdio_bus = (struct mac_bus_sdio *)priv;
    uint32 flag = disable_irq();
    skb->len  = 0;
    skb->data = skb->head;
    skb->tail = skb->head;
    skb->next = NULL;
    skb->prev = NULL;
    atomic_set(&skb->users, 1);
    sdio_bus->cmdskb = skb;
    os_printf("free cmdskb\r\n");
    enable_irq(flag);
}

struct mac_bus *mac_bus_sdio_attach(mac_bus_recv recv, void *priv)
{
    int ret = RET_OK;
    struct mac_bus_sdio *sdio_bus = os_zalloc(sizeof(struct mac_bus_sdio));
    ASSERT(sdio_bus && recv);
    os_mutex_init(&sdio_bus->tx_lock);
    sdio_bus->bus.write = sdio_bus_write;
    sdio_bus->bus.ioctl = sdio_bus_ioctl;
    sdio_bus->bus.type  = MAC_BUS_TYPE_SDIO;
    sdio_bus->bus.recv  = recv;
    sdio_bus->bus.priv  = priv;
    sdio_bus->cmdskb    = alloc_skb(1024);
    sdio_bus->cmdskb->free_priv = sdio_bus;
    sdio_bus->cmdskb->free = sdio_bus_cmdskb_free;
    sdio_bus->rx_buff   = (unsigned char *)(USER_POOL_START + USER_POOL_SIZE + 76);
    sdio_bus->dev = (struct sdio_slave *)dev_get(HG_SDIOSLAVE_DEVID);
    ASSERT(sdio_bus->dev);
    ret = sdio_slave_open(sdio_bus->dev, SDIO_SLAVE_SPEED_48M, 0); //SDIO_SLAVE_SPEED_4M
    ASSERT(!ret);
    os_printf("sdio open, ret=%d\n", ret);
    SYSMONITOR_CFG p_sysmonitor;
    p_sysmonitor.sysmonitor_ch  = SYSMONITOR_CH2_SDIO_DMA;
    //p_sysmonitor.chx_limit_low  = USER_POOL_START;
    //p_sysmonitor.chx_limit_high = USER_POOL_START + USER_POOL_SIZE;
    p_sysmonitor.chx_limit_low  = (unsigned int)(USER_POOL_START + USER_POOL_SIZE);
    p_sysmonitor.chx_limit_high = p_sysmonitor.chx_limit_low + 4096;
    sys_monitor_chan_config(&p_sysmonitor);
    sdio_slave_read(sdio_bus->dev, sdio_bus->rx_buff, (2048-76), FALSE);
    ASSERT(!ret);
    sdio_slave_request_irq(sdio_bus->dev, sdio_bus_irq, (uint32)sdio_bus);
    ASSERT(!ret);
    return &sdio_bus->bus;
}

__init void mac_bus_sdio_detach(struct mac_bus *bus)
{
    ASSERT(bus);
    struct mac_bus_sdio *sdio_bus = container_of(bus, struct mac_bus_sdio, bus);
    sdio_slave_close(sdio_bus->dev);
    os_mutex_del(&sdio_bus->tx_lock);
    os_free(sdio_bus);
}


