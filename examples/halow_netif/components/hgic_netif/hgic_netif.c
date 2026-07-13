/*
 * esp_netif driver for the Taixin/HUGE-IC (hgic) Wi-Fi HaLow module.
 * License: GPL 3.0
 */
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "hgic_netif.h"
#include "hgic_raw.h"

static const char *TAG = "hgic_netif";

typedef struct {
    esp_netif_driver_base_t base;
    SemaphoreHandle_t tx_lock;
} hgic_driver_t;

/*
 * hgic_raw_send_ether() serialises into the single shared hgic.tx_buf, so it
 * must not be re-entered. It returns the number of bytes handed to the SPI
 * layer -- a positive value -- on success, and -1 on failure. (The vendor's
 * lwIP demo treats any non-zero return as an error, which is backwards.)
 */
static esp_err_t hgic_transmit(void *h, void *buffer, size_t len)
{
    hgic_driver_t *drv = (hgic_driver_t *)h;

    if (len > HGIC_RAW_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }

    xSemaphoreTake(drv->tx_lock, portMAX_DELAY);
    int written = hgic_raw_send_ether((unsigned char *)buffer, (unsigned int)len);
    xSemaphoreGive(drv->tx_lock);

    return written > 0 ? ESP_OK : ESP_FAIL;
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
    drv->tx_lock = xSemaphoreCreateMutex();

    if (esp_netif_attach(netif, drv) != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_attach failed");
        free(drv);
        esp_netif_destroy(netif);
        return NULL;
    }

    ESP_ERROR_CHECK(esp_netif_set_mac(netif, (uint8_t *)mac));

    /* Bring the interface up. The link stays down until hgic_netif_set_link(). */
    esp_netif_action_start(netif, NULL, 0, NULL);

    ESP_LOGI(TAG, "created, mac %02x:%02x:%02x:%02x:%02x:%02x",
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
