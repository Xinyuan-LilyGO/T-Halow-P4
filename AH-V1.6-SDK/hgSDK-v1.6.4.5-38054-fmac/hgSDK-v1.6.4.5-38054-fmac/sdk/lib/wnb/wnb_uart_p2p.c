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

#define UART_P2P_PORT 61234

void uart_p2p_recv(uint8 *data, uint32 len);

int32 wnb_uart_p2p_send_ether_data(struct hgic_tx_info *txinfo, uint8 *data, int32 len)
{
	int32 err = RET_OK;

    struct sk_buff *skb;
    struct udpdata_info udp;
	struct wireless_nb *wnb = sysvar(SYSVAR_ID_WIRELESS_NB);

    udp.dest    	= (uint8 *)wnb_bcast_addr;
    udp.src     	= wnb->cfg->addr;
    udp.dest_ip 	= 0xffffffff;
    udp.src_ip  	= 0;
    udp.dest_port 	= UART_P2P_PORT;
    udp.src_port  	= UART_P2P_PORT;
    udp.data    	= data;
    udp.len     	= len;
	
    skb = net_setup_udpdata(&udp);
    if (skb) {
        err = wnb_send_ether_data(txinfo, skb->data, skb->len);
		kfree_skb(skb);
		
		return err;
    }
	
	return RET_OK;
}

static int32 wnb_uart_p2p_proc_rx(struct wireless_nb *wnb, struct wnb_rx_info *rxinfo)
{
    uint8 *data;
	uint16 len;
    uint8 *proto = rxinfo->data + 14 + 9;
	uint32 sport = get_unaligned_be16(rxinfo->data + 14 + 20);
    uint16 dport = get_unaligned_be16(rxinfo->data + 14 + 20 + 2);

    if (*proto == 0x11 && dport == UART_P2P_PORT && sport == UART_P2P_PORT && rxinfo->len >= 43) {
		data = rxinfo->data + 14 + 20 + 8;
		len  = rxinfo->len  - 14 - 20 - 8;
#ifdef UART_P2P_DEV
		uart_p2p_recv(data, len);
#endif
		return 1;
    }
    return 0;
}

static struct wnb_pkt_hook uart_p2p_hook = {"uart_p2p", 0x0800, NULL, wnb_uart_p2p_proc_rx};
__init __weak void wnb_module_uart_p2p_init(void)
{
    wnb_register_hook(&uart_p2p_hook);
}

