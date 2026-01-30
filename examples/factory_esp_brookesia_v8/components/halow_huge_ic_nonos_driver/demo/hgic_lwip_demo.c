#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "hgic_raw.h"
#include "netif.h"

/*
*   This is Hugeic LWIP ethernetif sample code
*   How to Use:
*   You can copy or reference following code on lwip/src/netif/ethernetif.c
*/

struct ethernetif {
    unsigned char tx_buff[2048];
    /* Add whatever per-interface state that is needed here. */
    unsigned char *rx_buff;
    unsigned int  rx_data_len;
};

static struct netif hgic_wifi;
extern struct hgic_raw hgic;

static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    struct ethernetif *ethernetif = netif->state;
    int ret = 0;

#if ETH_PAD_SIZE
    pbuf_header(p, -ETH_PAD_SIZE); /* drop the padding word */
#endif

    pbuf_copy_partial(p, ethernetif->tx_buff, p->tot_len, 0);
    //You can also reduce memcpy by modified hgic.tx_buf;

    ret = hgic_raw_send_ether(ethernetif->tx_buff, p->tot_len);
    if (ret) {
        printf("Send ethernet firm error,ret:%d\n", ret);
        return ret;
    }
    // Pay attention to your iface return!!!
    // do something according to your return value;
#if ETH_PAD_SIZE
    pbuf_header(p, ETH_PAD_SIZE); /* reclaim the padding word */
#endif
    return ERR_OK;
}

static struct pbuf *low_level_input(struct netif *netif)
{
    struct ethernetif *ethernetif = netif->state;
    struct pbuf *p = NULL,
    struct pbuf* q = NULL;
    u16_t len = 0;

    /* Obtain the size of the packet and put it into the "len"
       variable. */
    len = ethernetif->rx_data_len;

#if ETH_PAD_SIZE
    len += ETH_PAD_SIZE; /* allow room for Ethernet padding */
#endif

    /* We allocate a pbuf chain of pbufs from the pool. */
    p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);

    if (p != NULL) {

#if ETH_PAD_SIZE
        pbuf_header(p, -ETH_PAD_SIZE); /* drop the padding word */
#endif
        pbuf_take(p, ethernetif->rx_buff, ethernetif->rx_data_len);

#if ETH_PAD_SIZE
        pbuf_header(p, ETH_PAD_SIZE); /* reclaim the padding word */
#endif

    } else {
        printf("No memory,Drop packet!\n");
    }
    return p;
}

/*
*   Recv packet to lwip ethernet layer
*   Use : Calling it when wifi recv HGIC_HDR_TYPE_FRM2
*/

void hgic_netif_rx(unsigned char *data, unsigned int len)
{
    struct netif *wifi = &hgic_wifi;
    struct ethernetif *eth_if = (struct ethernetif *)wifi->state;

    eth_if->rx_buff     = data;
    eth_if->rx_data_len = len;
    ethernetif_input(wifi);
}

/*
*   Add hgic wifi to LWIP
*   Use:
*   You should send cmd HGIC_CMD_GET_FW_INFO to get wifi macaddr before 
*   calling hgic_lwip_netif_add
*/

int hgic_lwip_netif_add(void)
{
    struct ethernetif *ethif = NULL;

    MEMSET(&hgic_wifi, 0, sizeof(struct netif));
    MEMCPY(hgic_wifi.hwaddr, hgic.fwinfo.mac, 6);
    //Must send cmd:HGIC_CMD_GET_FW_INFO to get wifi macaddr first!!!  

    ethif = MALLOC(sizeof(struct ethernetif));
    ASSERT(ethif);
    MEMSET(ethif, 0, sizeof(struct ethernetif));

    if (NULL == netif_add(&hgic_wifi, IP_ADDR_ANY, IP_ADDR_ANY, IP_ADDR_ANY,
                          (void *)ethif, ethernetif_init, tcpip_input)) {
        printf("Add to netif failed!\n");
        return -EFAULT;
    }
    hgic_wifi.name[0] = 'w';
    hgic_wifi.name[1] = '0';

    netif_set_default(&hgic_wifi);
    netif_set_up(&hgic_wifi);
    netif_set_link_up(&hgic_wifi);
    return 0;
}
