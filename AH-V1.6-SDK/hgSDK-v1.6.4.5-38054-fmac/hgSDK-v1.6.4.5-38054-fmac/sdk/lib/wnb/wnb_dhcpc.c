#include "typesdef.h"
#include "errno.h"
#include "list.h"
#include "dev.h"
#include "devid.h"
#include "list.h"
#include "osal/string.h"
#include "osal/mutex.h"
#include "osal/semaphore.h"
#include "osal/mutex.h"
#include "osal/task.h"
#include "osal/timer.h"
#include "hal/gpio.h"
#include "lib/sysvar.h"
#include "lib/skb/skb.h"
#include "lib/mac_bus.h"
#include "lib/skb/skb_list.h"
#include "lib/lmac/hgic.h"
#include "lib/lmac/lmac.h"
#include "lib/lmac/lmac_ah/ieee802_11_defs.h"
#include "lib/wnb/libwnb.h"
#include "lib/syscfg.h"
#include "lib/ota/libota.h"
#include "lib/net/utils.h"
#include "syscfg_factory.h"

static void wnb_send_dhcp_request(struct wireless_nb *wnb, int8 type)
{
    uint8 *ptr;
    struct udpdata_info udp;
    struct sk_buff *skb = NULL;
    uint8  hostname = os_strlen(wnb->cfg->dhcpc_hostname);
    uint8 *request  = os_zalloc(512);
    if (request) {
        wnb_err("send %s\r\n", type==1?"DISCOVER":"REQUEST");
        ptr = request;
        *ptr++ = 1;
        *ptr++ = 1;
        *ptr++ = 6;
        *ptr++ = 0;
        if (wnb->dhcpc.transID == 0) wnb_random((uint8 *)&wnb->dhcpc.transID, 4);
        put_unaligned_be32(wnb->dhcpc.transID, ptr); ptr += 4;
        ptr += 2;
        //if(type == 1) put_unaligned_be16(0x8000, ptr);
        ptr += 2;
        ptr += 4;
        ptr += 4;
        ptr += 4;
        ptr += 4;
        os_memcpy(ptr, wnb->cfg->addr, 6); ptr += 6;
        ptr += 10;
        ptr += 64;
        ptr += 128;
        put_unaligned_be32(0x63825363, ptr); ptr += 4;

        *ptr++ = 53;
        *ptr++ = 1;
        *ptr++ = type;

        if (wnb->dhcpc.result.ipaddr) {
            *ptr++ = 50;
            *ptr++ = 4;
            put_unaligned_be32(wnb->dhcpc.result.ipaddr, ptr); ptr += 4;
        }

        *ptr++ = 61;
        *ptr++ = 7;
        *ptr++ = 1;
        os_memcpy(ptr, wnb->cfg->addr, 6); ptr += 6;


        if (hostname > 0) {
            *ptr++ = 12;
            *ptr++ = hostname;
            os_strcpy(ptr, wnb->cfg->dhcpc_hostname); ptr += hostname;
        }

        *ptr++ = 55;
        *ptr++ = 3;
        *ptr++ = 1;
        *ptr++ = 3;
        *ptr++ = 6;

        *ptr = 0xff;

        udp.dest = (uint8 *)wnb_bcast_addr;
        udp.src  = wnb->cfg->addr;
        udp.dest_ip = 0xffffffff;
        udp.dest_port = 67;
        udp.src_ip = 0;
        udp.src_port = 68;
        udp.data = request;
        udp.len  = (uint32)ptr - (uint32)request + 1;
        skb = net_setup_udpdata(&udp);
        if (skb) {
            wnb_lptable_update(wnb, skb->data, skb->data+6);
            skb_list_queue(&wnb->datalist, skb);
            WNB_RUN(wnb);
        }
        os_free(request);
    }
}

static void wnb_dhcpc_timer_cb(void *args)
{
    struct wireless_nb *wnb = (struct wireless_nb *)args;
    if(wnb->cfg->dhcpc_en)
        WNB_EVENT(wnb, WNB_EVENT_DHCPC_REQ);
}

static void wnb_dhcpc_do_request(struct wireless_nb *wnb)
{
    uint32 time = 500;

    if (wnb->state == WNB_STATE_CONNECTED) {
        switch (wnb->dhcpc.state) {
            case 5://ACK
                time = (wnb->dhcpc.leasetime / 2) * 1000;
                wnb->dhcpc.transID = 0;
                wnb->dhcpc.state   = 2;
                wnb_psalive_action(wnb, WNB_PSALIVE_ACTION_UPDATE_HBDATA);
                wnb_err("setup dhcp lease timer: %ds\r\n", wnb->dhcpc.leasetime / 2);
                break;
            case 2://OFFER
                wnb_send_dhcp_request(wnb, 3);
                break;
            default:
                wnb_send_dhcp_request(wnb, 1);
                break;
        }
    }
    if(wnb->cfg->dhcpc_en)
        os_timer_start(&wnb->dhcpc.timer, os_msecs_to_jiffies(time));
}

static void wnb_parse_dhcp_resp(struct wireless_nb *wnb, struct wnb_rx_info *rxinfo)
{
    uint8 *data   = rxinfo->data + 42;
    uint8 *optmsg = data + 240;

    os_memcpy(wnb->dhcpc.svraddr, rxinfo->data + 6, 6);
    wnb->dhcpc.result.ipaddr = get_unaligned_be32(data + 16); //your ip address
    if(wnb->psalive_data) wnb->psalive_data->dhcpc_ip = wnb->dhcpc.result.ipaddr;
    while (optmsg < (rxinfo->data + rxinfo->len) && optmsg[0] != 0xff) {
        switch (optmsg[0]) {
            case 1: //subnet mask
                wnb->dhcpc.result.netmask = get_unaligned_be32(optmsg + 2);
                break;
            case 3: //router
                wnb->dhcpc.result.router = get_unaligned_be32(optmsg + 2);
                break;
            case 6: //dns
                wnb->dhcpc.result.dns1 = get_unaligned_be32(optmsg + 2);
                wnb->dhcpc.result.dns2 = get_unaligned_be32(optmsg + 6);
                break;
            case 51: //leasetime
                wnb->dhcpc.leasetime = get_unaligned_be32(optmsg + 2);
                break;
            case 53: //message type
                //wnb->dhcpc.transID = 0;
                wnb->dhcpc.state = optmsg[2];
                if (wnb->dhcpc.state == 6) wnb->dhcpc.result.ipaddr = 0;
                else if(wnb->dhcpc.state == 5) WNB_EVENT(wnb, WNB_EVENT_DHCPC_DONE);
                else if(wnb->dhcpc.state == 2) wnb_send_dhcp_request(wnb, 3);
                break;
            case 54: //dhcp server
                wnb->dhcpc.result.svrip = get_unaligned_be32(optmsg + 2);
                break;
            default:
                break;
        }
        optmsg += (2 + optmsg[1]);
    }

    wnb_err("recv %d, ip:"IPSTR", netmask:"IPSTR", router:"IPSTR", svrip:"IPSTR", dns:"IPSTR"/"IPSTR", leasetime:%ds\r\n",
            wnb->dhcpc.state, IP2STR(wnb->dhcpc.result.ipaddr), IP2STR(wnb->dhcpc.result.netmask), IP2STR(wnb->dhcpc.result.router),
            IP2STR(wnb->dhcpc.result.svrip), IP2STR(wnb->dhcpc.result.dns1), IP2STR(wnb->dhcpc.result.dns2), wnb->dhcpc.leasetime);
}

static int32 wnb_dhcpc_proc_rx(struct wireless_nb *wnb, struct wnb_rx_info *rxinfo)
{
    uint32 sport, dport, trans_id;
    uint8 *proto = rxinfo->data + 14 + 9;
    uint8 *data  = rxinfo->data + 42;
    uint8 *caddr = data + 28;

    if (wnb->cfg->dhcpc_en && wnb->cfg->role == WNB_STA_ROLE_SLAVE && *proto == 17 && rxinfo->len > 240) {
        sport = get_unaligned_be16(rxinfo->data + 14 + 20);
        dport = get_unaligned_be16(rxinfo->data + 14 + 22);
        if (sport == 67 && dport == 68 && data[0] == 2 && MAC_EQU(caddr, wnb->cfg->addr)) {
            trans_id = get_unaligned_be32(data + 4);
            if (trans_id == wnb->dhcpc.transID) {
                wnb_parse_dhcp_resp(wnb, rxinfo);
                return 1;
            }
        }
    }
    return 0;
}

static void wnb_dhcpc_do_reset(struct wireless_nb *wnb)
{
    os_timer_stop(&wnb->dhcpc.timer);
    if (wnb->cfg->role == WNB_STA_ROLE_SLAVE && wnb->cfg->dhcpc_en) {
        os_timer_start(&wnb->dhcpc.timer, os_msecs_to_jiffies(10));
    }
}

static struct wnb_pkt_hook dhcpc_hook = {"dhcpc", 0x0800, NULL, wnb_dhcpc_proc_rx};
__init __weak void wnb_module_dhcpc_init(void)
{
    struct wireless_nb *wnb = sysvar(SYSVAR_ID_WIRELESS_NB);
    wnb->dhcpc.reset = wnb_dhcpc_do_reset;
    wnb->dhcpc.request_ip = wnb_dhcpc_do_request;
    wnb->dhcpc.result.ipaddr = (wnb->psalive_data ? wnb->psalive_data->dhcpc_ip : 0);
    os_timer_init(&wnb->dhcpc.timer, wnb_dhcpc_timer_cb, OS_TIMER_MODE_ONCE, wnb);
    wnb_register_hook(&dhcpc_hook);
    if (wnb->cfg->role == WNB_STA_ROLE_SLAVE && wnb->cfg->dhcpc_en) {
        os_timer_start(&wnb->dhcpc.timer, 10);
    }
}

