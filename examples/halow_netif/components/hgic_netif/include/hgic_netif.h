/*
 * esp_netif driver for the Taixin/HUGE-IC (hgic) Wi-Fi HaLow module.
 *
 * The vendor driver already presents a complete ethernet frame on both edges:
 *   TX  hgic_raw_send_ether(frame, len)
 *   RX  hgic_raw_rx(&frame, &len) == HGIC_RAW_RX_TYPE_DATA
 * so all that is needed is to hang those two calls off an esp_netif driver, the
 * same way esp_hosted does.
 *
 * Do not use the vendor's demo/hgic_lwip_demo.c: it targets raw lwIP (which
 * ESP-IDF hides behind esp_netif), it does not compile, and it could not link
 * even if it did -- see the raw_send() note in the vendor component CMakeLists.
 *
 * License: GPL 3.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Create and start the HaLow netif.
 *
 * `mac` must be the module's own MAC. That is only populated once the
 * HGIC_CMD_GET_FW_INFO *response* has been consumed by hgic_raw_rx() --
 * hgic_raw_get_fwinfo() merely writes the request and returns. Pass hgic.addr
 * after pumping RX until it is non-zero, or the interface comes up with an
 * all-zero hardware address.
 *
 * esp_netif_init() and esp_event_loop_create_default() must already have run.
 * Returns NULL on failure.
 */
esp_netif_t *hgic_netif_create(const uint8_t mac[6]);

/* Stop the DHCP client and assign a fixed address. */
esp_err_t hgic_netif_set_static_ip(esp_netif_t *netif, const char *ip,
                                   const char *netmask, const char *gw);

/*
 * Start the DHCP client instead. A bare HaLow BSS has no DHCP server, so this
 * is only useful when the AP-side module bridges onto an ethernet segment that
 * does have one.
 */
esp_err_t hgic_netif_use_dhcp(esp_netif_t *netif);

/*
 * Report the radio's association state. Drive this from hgic.status.conn_state,
 * which the vendor driver maintains from HGIC_EVENT_CONECTED / DISCONECTED as
 * those events are consumed by hgic_raw_rx().
 */
void hgic_netif_set_link(esp_netif_t *netif, bool up);

/*
 * Push one received ethernet frame into the stack. Safe to call from the SPI RX
 * task; the frame is copied, so the caller's buffer may be reused immediately.
 */
void hgic_netif_input(esp_netif_t *netif, const void *frame, size_t len);

#ifdef __cplusplus
}
#endif
