/*
 * esp_netif driver for the Taixin/HUGE-IC (hgic) Wi-Fi HaLow module.
 * License: GPL 3.0
 */
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "hgic_netif.h"
#include "hgic_raw.h"

static const char *TAG = "hgic_netif";

typedef struct {
    esp_netif_driver_base_t base;
} hgic_driver_t;

/*
 * Guards all access to the vendor driver's SPI bus and hgic global state --
 * shared between TX (hgic_transmit, below) and the RX pump (via the exported
 * hgic_netif_spi_lock/unlock). One module-wide instance: there is a single
 * radio. Statically allocated -- callers lock during radio bring-up, which
 * happens before hgic_netif_create().
 */
static StaticSemaphore_t s_spi_lock_buf;
static SemaphoreHandle_t s_spi_lock;

static SemaphoreHandle_t spi_lock(void)
{
    if (s_spi_lock == NULL) {
        s_spi_lock = xSemaphoreCreateMutexStatic(&s_spi_lock_buf);
    }
    return s_spi_lock;
}

void hgic_netif_spi_lock(void)   { xSemaphoreTake(spi_lock(), portMAX_DELAY); }
void hgic_netif_spi_unlock(void) { xSemaphoreGive(spi_lock()); }

/*
 * HaLow egress (TX) queue.
 *
 * lwIP calls hgic_transmit() from the tcpip thread. Sending synchronously over
 * SPI there blocks that thread, and when the radio is slower than the ingress
 * (a WiFi->HaLow router under uplink load), frames pile up with nothing to
 * signal upstream -- the SDIO/host side never sees the pressure because it
 * drains fine; the bottleneck is here, at the radio. So we copy each frame
 * into a bounded queue drained by a dedicated TX task, and use the queue depth
 * as the egress-backlog signal: crossing the high-water mark asks the app to
 * throttle its ingress, dropping below the low mark releases it. Queue-full is
 * a plain drop (TCP retransmits; UDP is expendable).
 */
#define HGIC_TX_Q_DEPTH   32
#define HGIC_TX_Q_HIGH    24   /* ask ingress to pause at 75% full */
#define HGIC_TX_Q_LOW      8   /* release ingress at 25% full     */

typedef struct {
    uint16_t len;
    uint8_t *data;   /* malloc'd copy, freed by the TX task */
} hgic_tx_msg_t;

static QueueHandle_t s_tx_queue;
static volatile bool s_egress_throttled;

/*
 * Weak hook: an application that forwards onto the radio overrides this to
 * connect HaLow egress backlog to its own ingress flow control (the router
 * wires it to esp_hosted slave-RX pause). Default no-op.
 */
__attribute__((weak)) void hgic_netif_egress_throttle(bool pause) { (void)pause; }

/*
 * hgic_raw_send_ether() serialises into the single shared hgic.tx_buf, so it
 * must not be re-entered; the single TX task is the only sender. It returns
 * the bytes handed to SPI (positive) on success, -1 on failure.
 */
static void hgic_tx_task(void *arg)
{
    (void)arg;
    hgic_tx_msg_t msg;
    for (;;) {
        if (xQueueReceive(s_tx_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        hgic_netif_spi_lock();
        hgic_raw_send_ether(msg.data, msg.len);
        hgic_netif_spi_unlock();
        free(msg.data);

        if (s_egress_throttled &&
            uxQueueMessagesWaiting(s_tx_queue) <= HGIC_TX_Q_LOW) {
            s_egress_throttled = false;
            hgic_netif_egress_throttle(false);
        }
    }
}

static void hgic_tx_start(void)
{
    if (s_tx_queue) {
        return;
    }
    s_tx_queue = xQueueCreate(HGIC_TX_Q_DEPTH, sizeof(hgic_tx_msg_t));
    if (s_tx_queue == NULL) {
        ESP_LOGE(TAG, "tx queue alloc failed");
        return;
    }
    xTaskCreate(hgic_tx_task, "hgic_tx", 4096, NULL, 10, NULL);
}

static esp_err_t hgic_transmit(void *h, void *buffer, size_t len)
{
    (void)h;

    if (len > HGIC_RAW_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (s_tx_queue == NULL) {
        return ESP_FAIL;
    }

    uint8_t *copy = malloc(len);
    if (copy == NULL) {
        return ESP_OK;   /* out of memory -- drop this frame */
    }
    memcpy(copy, buffer, len);

    hgic_tx_msg_t msg = { .len = (uint16_t)len, .data = copy };
    if (xQueueSend(s_tx_queue, &msg, 0) != pdTRUE) {
        free(copy);      /* egress backlog full -- drop */
        return ESP_OK;
    }

    /* Egress backing up? ask the application to pause its ingress. */
    if (!s_egress_throttled &&
        uxQueueMessagesWaiting(s_tx_queue) >= HGIC_TX_Q_HIGH) {
        s_egress_throttled = true;
        hgic_netif_egress_throttle(true);
    }
    return ESP_OK;
}

static void hgic_free_rx(void *h, void *buffer)
{
    (void)h;
    free(buffer);
}

static esp_err_t hgic_post_attach(esp_netif_t *netif, void *args)
{
    hgic_driver_t *drv = (hgic_driver_t *)args;
    drv->base.netif = netif;

    const esp_netif_driver_ifconfig_t ifcfg = {
        .handle = drv,
        .transmit = hgic_transmit,
        .driver_free_rx_buffer = hgic_free_rx,
    };
    return esp_netif_set_driver_config(netif, &ifcfg);
}

esp_netif_t *hgic_netif_create(const uint8_t mac[6])
{
    esp_netif_inherent_config_t base_cfg = ESP_NETIF_INHERENT_DEFAULT_ETH();
    base_cfg.if_key = "HALOW_DEF";
    base_cfg.if_desc = "halow";

    const esp_netif_config_t cfg = {
        .base = &base_cfg,
        .driver = NULL,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
    };

    esp_netif_t *netif = esp_netif_new(&cfg);
    if (netif == NULL) {
        ESP_LOGE(TAG, "esp_netif_new failed");
        return NULL;
    }

    hgic_driver_t *drv = calloc(1, sizeof(*drv));
    if (drv == NULL) {
        esp_netif_destroy(netif);
        return NULL;
    }
    drv->base.post_attach = hgic_post_attach;

    if (esp_netif_attach(netif, drv) != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_attach failed");
        free(drv);
        esp_netif_destroy(netif);
        return NULL;
    }

    ESP_ERROR_CHECK(esp_netif_set_mac(netif, (uint8_t *)mac));

    /* Start the egress TX task before the interface can transmit. */
    hgic_tx_start();

    /* Bring the interface up. The link stays down until hgic_netif_set_link(). */
    esp_netif_action_start(netif, NULL, 0, NULL);

    ESP_LOGI(TAG, "created, mac %02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return netif;
}

/*
 * AP-side variant: static address plus a DHCP *server* for the HaLow BSS,
 * the shape a routed gateway needs (clients lease from us and we masquerade
 * them upstream). Same driver glue as hgic_netif_create().
 */
esp_netif_t *hgic_netif_create_dhcps(const uint8_t mac[6], const char *ip,
                                     const char *netmask)
{
    esp_netif_inherent_config_t base_cfg = ESP_NETIF_INHERENT_DEFAULT_ETH();
    base_cfg.if_key = "HALOW_AP";
    base_cfg.if_desc = "halow";
    base_cfg.flags = (esp_netif_flags_t)(ESP_NETIF_DHCP_SERVER | ESP_NETIF_FLAG_AUTOUP);

    esp_netif_ip_info_t ip_info = {0};
    ip_info.ip.addr = esp_ip4addr_aton(ip);
    ip_info.netmask.addr = esp_ip4addr_aton(netmask);
    ip_info.gw.addr = ip_info.ip.addr;   /* we are the gateway */
    base_cfg.ip_info = &ip_info;

    const esp_netif_config_t cfg = {
        .base = &base_cfg,
        .driver = NULL,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
    };

    esp_netif_t *netif = esp_netif_new(&cfg);
    if (netif == NULL) {
        ESP_LOGE(TAG, "esp_netif_new failed");
        return NULL;
    }

    hgic_driver_t *drv = calloc(1, sizeof(*drv));
    if (drv == NULL) {
        esp_netif_destroy(netif);
        return NULL;
    }
    drv->base.post_attach = hgic_post_attach;

    if (esp_netif_attach(netif, drv) != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_attach failed");
        free(drv);
        esp_netif_destroy(netif);
        return NULL;
    }

    ESP_ERROR_CHECK(esp_netif_set_mac(netif, (uint8_t *)mac));
    hgic_tx_start();
    esp_netif_action_start(netif, NULL, 0, NULL);

    ESP_LOGI(TAG, "created (dhcps %s), mac %02x:%02x:%02x:%02x:%02x:%02x", ip,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return netif;
}

esp_err_t hgic_netif_set_static_ip(esp_netif_t *netif, const char *ip,
                                   const char *netmask, const char *gw)
{
    esp_err_t err = esp_netif_dhcpc_stop(netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        return err;
    }

    esp_netif_ip_info_t ip_info = {0};
    ip_info.ip.addr = esp_ip4addr_aton(ip);
    ip_info.netmask.addr = esp_ip4addr_aton(netmask);
    ip_info.gw.addr = esp_ip4addr_aton(gw);

    err = esp_netif_set_ip_info(netif, &ip_info);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "static ip %s/%s gw %s", ip, netmask, gw);
    }
    return err;
}

esp_err_t hgic_netif_use_dhcp(esp_netif_t *netif)
{
    esp_err_t err = esp_netif_dhcpc_start(netif);
    if (err == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        err = ESP_OK;
    }
    return err;
}

void hgic_netif_set_link(esp_netif_t *netif, bool up)
{
    if (up) {
        esp_netif_action_connected(netif, NULL, 0, NULL);
    } else {
        esp_netif_action_disconnected(netif, NULL, 0, NULL);
    }
    ESP_LOGI(TAG, "link %s", up ? "up" : "down");
}

void hgic_netif_input(esp_netif_t *netif, const void *frame, size_t len)
{
    void *buf = malloc(len);
    if (buf == NULL) {
        ESP_LOGW(TAG, "rx drop, no mem (%u bytes)", (unsigned)len);
        return;
    }
    memcpy(buf, frame, len);

    /* On success esp_netif owns buf and releases it via hgic_free_rx(). */
    if (esp_netif_receive(netif, buf, len, NULL) != ESP_OK) {
        free(buf);
    }
}
