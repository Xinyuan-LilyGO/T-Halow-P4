
#include "typesdef.h"
#include "list.h"
#include "dev.h"
#include "devid.h"
#include "osal/string.h"
#include "osal/mutex.h"
#include "osal/task.h"
#include "osal/semaphore.h"
#include "hal/usb_device.h"
#include "lib/lmac/hgic.h"
#include "lib/skb/skb.h"
#include "lib/mac_bus.h"
#include "lib/usb_device_wifi.h"
#include "dev/usb/hgusb11_dev_api.h"
#include "dev/usb/hgusb11_dev_tbl.h"

#define USB_BUS_RXBUFF_SIZE (4096)

struct mac_bus_usb {
    struct mac_bus     bus;
    struct os_mutex    tx_lock;
    struct usb_device *dev;
    struct sk_buff    *cmdskb;
    /* address need 4byte aligned */
    uint8              rxbuff[USB_BUS_RXBUFF_SIZE];
    uint32             ready: 1,
                       resv: 31;
};

static int32 usb_bus_pading(uint8 *buff, int32 len)
{
    uint8 pad = (uint32)buff & 0x03;
    if(pad){
        os_memset(buff-pad, 0xff, pad);
    }
    return pad;
}

static int32 usb_bus_write(struct mac_bus *bus, uint8 *buff, int32 len)
{
    int32 ret = RET_OK;
    uint8 pad = 0;
    struct mac_bus_usb *usb_bus = container_of(bus, struct mac_bus_usb, bus);

    pad   = usb_bus_pading(buff, len);
    buff -= pad;
    len  += pad;

    if (!usb_bus->ready) {
        usb_bus->bus.txerr++;
        return RET_ERR;
    }

    ret = os_mutex_lock(&usb_bus->tx_lock, 500);
    if (ret) {
        usb_bus->bus.txerr++;
        os_printf("usb bus write wait tx_lock timeout, ret:%d\r\n", ret);
        return RET_ERR;
    }
    ret = usb_device_wifi_write(usb_bus->dev, (int8 *)buff, len);
    os_mutex_unlock(&usb_bus->tx_lock);
    if (ret) {
        usb_bus->bus.txerr++;
        usb_bus->ready = 0;
    }
    return ret;
}

static void usb_bus_irq(uint32 irq, uint32 param1, uint32 param2)
{
    struct mac_bus_usb *usb_bus = (struct mac_bus_usb *)param1;
    struct hgic_hdr *hdr = (struct hgic_hdr *)usb_bus->rxbuff;
    struct sk_buff *skb;

    switch (irq) {
        case USB_DEV_RESET_IRQ:
            usb_device_wifi_read(usb_bus->dev, (int8 *)usb_bus->rxbuff, USB_BUS_RXBUFF_SIZE);
            break;
        case USB_DEV_SUSPEND_IRQ:
            break;
        case USB_DEV_RESUME_IRQ:
            break;
        case USB_DEV_SOF_IRQ:
            break;
        case USB_DEV_CTL_IRQ:
            break;
        case USB_EP1_RX_IRQ:
            usb_bus->ready = 1;
            if (param2 > 1600) {
                os_printf("usb rxbuff over, %d\r\n", param2);
                usb_bus->bus.rxerr++;
            } else {
                skb = alloc_tx_skb(param2 + 256 + 32);
                if (skb) {
                    skb_reserve(skb, 256);
                    hw_memcpy(skb->data, usb_bus->rxbuff, param2);
                    usb_bus->bus.recv(&usb_bus->bus, skb, param2);
                }else if(hdr->magic == HGIC_HDR_TX_MAGIC &&
                        (hdr->type == HGIC_HDR_TYPE_CMD || hdr->type == HGIC_HDR_TYPE_CMD2 || hdr->type == HGIC_HDR_TYPE_BOOTDL)){ // only recv CMD/CMD2
                    skb = usb_bus->cmdskb;
                    usb_bus->cmdskb = NULL;
                    if(skb){
                        os_memcpy(skb->data, usb_bus->rxbuff, param2);
                        usb_bus->bus.recv(&usb_bus->bus, skb, param2);
                    }else{
                        usb_bus->bus.rxerr++;
                    }
                } else {
                    usb_bus->bus.rxerr++;
                }
            }
            os_memset(usb_bus->rxbuff, 0, 8);
            usb_device_wifi_read(usb_bus->dev, (int8 *)usb_bus->rxbuff, USB_BUS_RXBUFF_SIZE);
            break;
        case USB_EP1_TX_IRQ:
            os_mutex_unlock(&usb_bus->tx_lock);
            break;
        default:
            break;
    }
}

static void usb_bus_cmdskb_free(void *priv, struct sk_buff *skb)
{
    struct mac_bus_usb *usb_bus = (struct mac_bus_usb *)priv;
    uint32 flag = disable_irq();
    skb->len  = 0;
    skb->data = skb->head;
    skb->tail = skb->head;
    skb->next = NULL;
    skb->prev = NULL;
    atomic_set(&skb->users, 1);
    usb_bus->cmdskb = skb;
    enable_irq(flag);
}

__init struct mac_bus *mac_bus_usb_attach(mac_bus_recv recv, void *priv)
{
    struct mac_bus_usb *usb_bus;
    struct usb_device *usb = (struct usb_device *)dev_get(HG_USB11_DEV_DEVID);

    if (usb == NULL) {
        return NULL;
    }
    usb_bus = os_zalloc(sizeof(struct mac_bus_usb));
    ASSERT(usb_bus && recv);
    usb_bus->bus.write = usb_bus_write;
    usb_bus->bus.type  = MAC_BUS_TYPE_USB;
    usb_bus->bus.recv  = recv;
    usb_bus->bus.priv  = priv;
    usb_bus->dev = usb;
    usb_bus->cmdskb    = alloc_skb(1024);
    usb_bus->cmdskb->free_priv = usb_bus;
    usb_bus->cmdskb->free = usb_bus_cmdskb_free;
    os_mutex_init(&usb_bus->tx_lock);
    usb_device_wifi_open(usb_bus->dev);
    usb_device_wifi_auto_tx_null_pkt_enable(usb_bus->dev);
    usb_device_request_irq(usb_bus->dev, usb_bus_irq, (uint32)usb_bus);
    os_printf("USB bus init done\r\n");
    return &usb_bus->bus;
}

void mac_bus_usb_detach(struct mac_bus *bus)
{
    struct mac_bus_usb *usb_bus;
    if (bus) {
        usb_bus = container_of(bus, struct mac_bus_usb, bus);
        usb_device_wifi_auto_tx_null_pkt_disable(usb_bus->dev);
        usb_device_wifi_close(usb_bus->dev);
        os_mutex_del(&usb_bus->tx_lock);
        os_free(usb_bus);
    }
}

