/*
 * HaLow node on the T-Halow-P4: one image, two personalities, selected by the
 * persisted HaLow role (set from the config page, stored in NVS):
 *
 *  role "sta" -> ROUTER: clients join the C6 SoftAP and are NAT'd onto the
 *                HaLow link (the camera / remote end).
 *
 *     client ──WiFi──> C6 SoftAP ──SDIO──> P4 [NAPT] ──SPI──> HaLow STA )))
 *
 *  role "ap"  -> GATEWAY: the HaLow module is the AP, the P4 serves DHCP on
 *                the HaLow BSS and masquerades out over the home WiFi (the
 *                infrastructure end; routing keeps LAN broadcast chatter off
 *                the air entirely).
 *
 *     home LAN ──WiFi──> C6 STA ──SDIO──> P4 [DHCP+NAPT] ──SPI──> HaLow AP )))
 *
 * WiFi credentials come from NVS when saved via POST /api/wifi (portal card),
 * else from Kconfig defaults. Flip a board's personality from a browser:
 * set Role on the page (persists), save WiFi credentials, POST /api/reboot.
 *
 * Supersedes examples/halow_router and examples/halow_gateway (kept for
 * reference); shares their web server, AT bridge and OTA machinery.
 * License: GPL 3.0
 */
#include <cstring>
#include <memory>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_wifi_remote.h"
#include "esp_wifi.h"
#include "dhcpserver/dhcpserver.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "t_halow_p4_config.h"
#include "cpp_bus_driver_library.h"
#include "hgic_netif.h"

#include "at_uart.h"
#include "camera_stream.h"
#include "web_server.h"

#if CONFIG_LWIP_IPV4_NAPT_PORTMAP
#include "lwip/lwip_napt.h"
#include "lwip/prot/ip.h"
#endif

extern "C"
{
#include "hgic_sdspi.h"
#include "hgic_raw.h"
}

/* HaLow egress backpressure -> esp_hosted flow control (see halow_router). */
extern "C" void esp_hosted_wifi_rx_throttle(bool pause);
extern "C" void hgic_netif_egress_throttle(bool pause)
{
    esp_hosted_wifi_rx_throttle(pause);
}

#define MAX_SPI_RECEIVE_SIZE std::min(HGIC_RAW_DATA_ROOM, HGIC_RAW_MAX_PAYLOAD)
#define HGIC_RX_BUF_SIZE 2048

auto spidrv_read_buffer = std::make_unique<uint8_t[]>(MAX_SPI_RECEIVE_SIZE);

volatile bool Interrupt_Flag = false;

static bool s_gateway = false; /* personality: false = router, true = gateway */
static esp_netif_t *s_halow_netif = nullptr;
static esp_netif_t *s_wifi_netif = nullptr; /* SoftAP (router) or STA (gateway) */

static char s_ap_ssid[33];
static char s_ap_pass[65];
static char s_sta_ssid[33];
static char s_sta_pass[65];

static volatile uint32_t s_rx_frames = 0;
static volatile uint32_t s_rx_data_frames = 0;
static volatile uint32_t s_rx_chatter_dropped = 0;

auto Tx_Ah_R900pnr_Spi_Bus = std::make_shared<Cpp_Bus_Driver::Hardware_Spi>(
    TX_AH_R900PNR_MOSI, TX_AH_R900PNR_SCLK, TX_AH_R900PNR_MISO,
    SPI2_HOST, 0, spi_clock_source_t::SPI_CLK_SRC_SPLL);

// ---------------------------------------------------------------------------
// SPI glue required by the vendor driver (see halow_router for the raw_send
// rename note).
// ---------------------------------------------------------------------------

void spidrv_write_read(void *priv, unsigned char *wdata, unsigned char *rdata, unsigned int len)
{
    Tx_Ah_R900pnr_Spi_Bus->write_read(wdata, rdata, len);
}

void spidrv_write(void *priv, unsigned char *data, unsigned int len, char dma_flag)
{
    Tx_Ah_R900pnr_Spi_Bus->write(data, len);
}

void spidrv_read(void *priv, unsigned char *data, unsigned int len, char dma_flag)
{
    Tx_Ah_R900pnr_Spi_Bus->write_read(spidrv_read_buffer.get(), data, len);
}

void spidrv_cs(void *priv, char enable) {}

int spidrv_hw_crc(void *priv, unsigned char *data, unsigned int len, char flag)
{
    return 0;
}

int raw_send(unsigned char *data, unsigned int len)
{
    return hgic_sdspi_write(0, data, len);
}

// ---------------------------------------------------------------------------

/* Drop group-addressed HaLow ingress except ARP and IPv4 broadcast DHCP --
 * NAT never forwards it, so don't pay lwIP for the discard. */
static bool halow_chatter(const uint8_t *frame, size_t len)
{
    if (len < 14 || (frame[0] & 1) == 0)
    {
        return false;
    }
    uint16_t ethertype = (frame[12] << 8) | frame[13];
    if (ethertype == 0x0806)
    {
        return false;
    }
    if (ethertype == 0x0800 && len >= 14 + 20 + 8)
    {
        size_t ihl = (frame[14] & 0x0f) * 4;
        if (frame[23] == 17 && len >= 14 + ihl + 8)
        {
            uint16_t dport = (frame[14 + ihl + 2] << 8) | frame[14 + ihl + 3];
            if (dport == 67 || dport == 68)
            {
                return false;
            }
        }
    }
    return true;
}

static void hgic_pump_rx(void)
{
    static auto rx = std::make_unique<unsigned char[]>(HGIC_RX_BUF_SIZE);

    for (int i = 0; i < 8; i++)
    {
        hgic_netif_spi_lock();
        size_t length = hgic_sdspi_read(0, rx.get(), HGIC_RX_BUF_SIZE, 0);
        if (length == static_cast<size_t>(-1) || length == 0)
        {
            hgic_netif_spi_unlock();
            break;
        }

        unsigned char *p = rx.get();
        unsigned int len = static_cast<unsigned int>(length);
        s_rx_frames++;

        int type = hgic_raw_rx(&p, &len);
        hgic_netif_spi_unlock();

        if (type == HGIC_RAW_RX_TYPE_DATA && s_halow_netif != nullptr)
        {
            if (halow_chatter(p, len))
            {
                s_rx_chatter_dropped++;
                continue;
            }
            s_rx_data_frames++;
            hgic_netif_input(s_halow_netif, p, len);
        }
    }
}

static bool hgic_wait_for_mac(int timeout_ms)
{
    const uint8_t zero[6] = {0};
    int64_t deadline = esp_timer_get_time() / 1000 + timeout_ms;
    int64_t next_request = 0;

    while (esp_timer_get_time() / 1000 < deadline)
    {
        int64_t now = esp_timer_get_time() / 1000;
        if (now >= next_request)
        {
            hgic_raw_get_fwinfo();
            next_request = now + 500;
        }

        hgic_pump_rx();
        if (memcmp(hgic.addr, zero, 6) != 0)
        {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

// ---------------------------------------------------------------------------
// WiFi + DNS + NAT plumbing, both personalities.
// ---------------------------------------------------------------------------

#define DHCPS_OFFER_DNS 0x02

/* Set once the gateway has installed its forwards, so /api/status can say so. */
static bool s_portmap_up = false;

/* What is this node doing right now -- the answer you want when a board is
 * remote, has no console, and you need to know which image it is running. */
static esp_err_t status_get_handler(httpd_req_t *req)
{
    char json[320];
    snprintf(json, sizeof(json),
             "{\"role\":\"%s\",\"personality\":\"%s\",\"built\":\"%s %s\","
             "\"camera\":\"%s\",\"stream_port\":%d,\"portmap\":%s,"
             "\"uptime_s\":%lld,\"heap_dma\":%u}",
             s_gateway ? "ap" : "sta", s_gateway ? "gateway" : "router",
             __DATE__, __TIME__, camera_stream_state(), CONFIG_NODE_STREAM_PORT,
             s_portmap_up ? "true" : "false",
             (long long)(esp_timer_get_time() / 1000000),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

/* Which netif runs the DHCP server: the SoftAP (router) or HaLow (gateway). */
static esp_netif_t *dhcps_netif(void)
{
    return s_gateway ? s_halow_netif : s_wifi_netif;
}

static void offer_dns_to_clients(const esp_netif_dns_info_t *dns)
{
    esp_netif_t *n = dhcps_netif();
    if (n == nullptr)
    {
        return;
    }
    uint8_t opt = DHCPS_OFFER_DNS;
    esp_netif_dhcps_stop(n);
    ESP_ERROR_CHECK(esp_netif_dhcps_option(n, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
                                           &opt, sizeof(opt)));
    ESP_ERROR_CHECK(esp_netif_set_dns_info(n, ESP_NETIF_DNS_MAIN, (esp_netif_dns_info_t *)dns));
    esp_netif_dhcps_start(n);
}

/* Learn upstream DNS from a lease on `src` and re-offer it to our clients. */
static void follow_upstream_dns(esp_netif_t *src)
{
    esp_netif_dns_info_t dns;
    if (src != nullptr &&
        esp_netif_get_dns_info(src, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK &&
        dns.ip.u_addr.ip4.addr != 0)
    {
        offer_dns_to_clients(&dns);
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START)
    {
        /* Router personality: the SoftAP netif is only up now; NAT and the
         * client DNS option must wait for this event. */
        esp_netif_dns_info_t dns = {};
        dns.ip.u_addr.ip4.addr = esp_ip4addr_aton(CONFIG_NODE_DNS_FALLBACK);
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        offer_dns_to_clients(&dns);
        follow_upstream_dns(s_halow_netif);

        esp_err_t err = esp_netif_napt_enable(s_wifi_netif);
        printf("NAT %s on the softap subnet\n", err == ESP_OK ? "enabled" : "ENABLE FAILED");
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    {
        printf("wifi disconnected, retrying\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_wifi_connect();
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)data;
        printf("wifi client " MACSTR " joined\n", MAC2STR(e->mac));
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        /* Gateway personality: home-WiFi lease arrived. */
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        printf("wifi ip " IPSTR " gw " IPSTR "\n", IP2STR(&e->ip_info.ip), IP2STR(&e->ip_info.gw));
        follow_upstream_dns(s_wifi_netif);

#if CONFIG_LWIP_IPV4_NAPT_PORTMAP && CONFIG_NODE_STREAM_FORWARD
        /* Punch two ports through our NAT to the field unit: its H.264 stream
         * so the home LAN can watch, and its config portal so the field unit
         * can be reconfigured and OTA'd remotely -- over HaLow, which stays
         * usable even when the far end's own WiFi side is in trouble. */
        if (s_gateway)
        {
            uint32_t target = esp_ip4addr_aton(CONFIG_NODE_STREAM_FORWARD_IP);
            ip_portmap_add(IP_PROTO_TCP, e->ip_info.ip.addr, CONFIG_NODE_STREAM_PORT,
                           target, CONFIG_NODE_STREAM_PORT);
            ip_portmap_add(IP_PROTO_TCP, e->ip_info.ip.addr, CONFIG_NODE_MGMT_FORWARD_PORT,
                           target, 80);
            printf("forwarding to %s: tcp :%d -> :%d (stream), tcp :%d -> :80 (portal)\n",
                   CONFIG_NODE_STREAM_FORWARD_IP, CONFIG_NODE_STREAM_PORT, CONFIG_NODE_STREAM_PORT,
                   CONFIG_NODE_MGMT_FORWARD_PORT);
            s_portmap_up = true;
        }
#endif
    }
    else if (base == IP_EVENT && id == IP_EVENT_ETH_GOT_IP)
    {
        /* Router personality: HaLow (ethernet-inherent) lease arrived. */
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        printf("halow ip " IPSTR " gw " IPSTR "\n", IP2STR(&e->ip_info.ip), IP2STR(&e->ip_info.gw));
        follow_upstream_dns(s_halow_netif);
    }
}

static void wifi_start(void)
{
    if (s_gateway)
    {
        s_wifi_netif = esp_netif_create_default_wifi_sta();
    }
    else
    {
        s_wifi_netif = esp_netif_create_default_wifi_ap();
    }
    assert(s_wifi_netif != nullptr);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));

    wifi_config_t wc = {};
    if (s_gateway)
    {
        strlcpy((char *)wc.sta.ssid, s_sta_ssid, sizeof(wc.sta.ssid));
        strlcpy((char *)wc.sta.password, s_sta_pass, sizeof(wc.sta.password));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
        ESP_ERROR_CHECK(esp_wifi_start());
        printf("wifi sta joining \"%s\"\n", s_sta_ssid);
    }
    else
    {
        strlcpy((char *)wc.ap.ssid, s_ap_ssid, sizeof(wc.ap.ssid));
        wc.ap.ssid_len = strlen(s_ap_ssid);
        wc.ap.channel = CONFIG_NODE_WIFI_AP_CHANNEL;
        wc.ap.max_connection = 4;
        if (strlen(s_ap_pass) > 0)
        {
            strlcpy((char *)wc.ap.password, s_ap_pass, sizeof(wc.ap.password));
            wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
        }
        else
        {
            wc.ap.authmode = WIFI_AUTH_OPEN;
        }
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
        ESP_ERROR_CHECK(esp_wifi_start());
        printf("wifi softap \"%s\" up, config page on http://192.168.4.1/\n", s_ap_ssid);
    }
}

// The HaLow SPI service, pinned to core 1 (see halow_router).
static void halow_pump_task(void *arg)
{
    hgic_netif_spi_lock();
    hgic_raw_get_connect_state();
    hgic_netif_spi_unlock();

    int last_link = -1;
    int64_t next_poll = 0;
    int64_t next_stats = 0;

    while (1)
    {
        hgic_netif_spi_lock();
        if (hgic_sdspi_detect_alive(0) == -1)
        {
            hgic_sdspi_init(0);
        }
        hgic_netif_spi_unlock();

        if (Interrupt_Flag)
        {
            Interrupt_Flag = false;
            hgic_pump_rx();
        }

        int64_t now = esp_timer_get_time() / 1000;
        if (now >= next_poll)
        {
            next_poll = now + 1000;
            hgic_netif_spi_lock();
            hgic_raw_get_connect_state();
            hgic_netif_spi_unlock();
        }

        /* Router personality: track association. Gateway: link forced up. */
        if (!s_gateway && hgic.status.conn_state != last_link)
        {
            last_link = hgic.status.conn_state;
            hgic_netif_set_link(s_halow_netif, last_link != 0);
        }

        if (now >= next_stats)
        {
            next_stats = now + 10000;
            printf("halow rx: frames=%lu data=%lu chatter-dropped=%lu stas=%u\n",
                   (unsigned long)s_rx_frames, (unsigned long)s_rx_data_frames,
                   (unsigned long)s_rx_chatter_dropped, (unsigned)hgic.sta_cnt);
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

extern "C" void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* Personality + WiFi credentials: Kconfig defaults, NVS overrides. */
#if CONFIG_NODE_DEFAULT_ROLE_AP
    char role[8] = "ap";
#else
    char role[8] = "sta";
#endif
    web_server_get_halow_role(role, sizeof(role));
    s_gateway = (strcmp(role, "ap") == 0);

    strlcpy(s_ap_ssid, CONFIG_NODE_WIFI_AP_SSID, sizeof(s_ap_ssid));
    strlcpy(s_ap_pass, CONFIG_NODE_WIFI_AP_PASSWORD, sizeof(s_ap_pass));
    strlcpy(s_sta_ssid, CONFIG_NODE_WIFI_STA_SSID, sizeof(s_sta_ssid));
    strlcpy(s_sta_pass, CONFIG_NODE_WIFI_STA_PASSWORD, sizeof(s_sta_pass));
    web_server_get_nvs_str("ap_ssid", s_ap_ssid, sizeof(s_ap_ssid));
    web_server_get_nvs_str("ap_pass", s_ap_pass, sizeof(s_ap_pass));
    web_server_get_nvs_str("sta_ssid", s_sta_ssid, sizeof(s_sta_ssid));
    web_server_get_nvs_str("sta_pass", s_sta_pass, sizeof(s_sta_pass));

    printf("halow node personality: %s\n", s_gateway ? "GATEWAY (halow ap)" : "ROUTER (halow sta)");

    Tx_Ah_R900pnr_Spi_Bus->create_gpio_interrupt(
        TX_AH_R900PNR_INT, Cpp_Bus_Driver::Tool::Interrupt_Mode::FALLING,
        [](void *arg) -> IRAM_ATTR void
        {
            Interrupt_Flag = true;
        });

    memset(spidrv_read_buffer.get(), 0xFF, MAX_SPI_RECEIVE_SIZE);
    Tx_Ah_R900pnr_Spi_Bus->begin(40000000, TX_AH_R900PNR_CS);
    vTaskDelay(pdMS_TO_TICKS(1000));

    while (hgic_sdspi_init(0) == -1)
    {
        printf("hgic_sdspi_init fail, retrying\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    printf("hgic_sdspi_init success\n");

    bool halow_ok = hgic_wait_for_mac(5000);
    if (halow_ok)
    {
        printf("halow role: %s\n", role);
        hgic_raw_set_mode(role);
    }
    else
    {
        printf("hgic_raw_get_fwinfo: no response, HaLow side disabled\n");
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if (halow_ok)
    {
        if (s_gateway)
        {
            s_halow_netif = hgic_netif_create_dhcps(hgic.addr, CONFIG_NODE_HALOW_GW_IP,
                                                    CONFIG_NODE_HALOW_GW_NETMASK);
        }
        else
        {
            s_halow_netif = hgic_netif_create(hgic.addr);
        }
        if (s_halow_netif == nullptr)
        {
            printf("hgic netif create fail\n");
            return;
        }

        if (s_gateway)
        {
            esp_netif_dns_info_t dns = {};
            dns.ip.u_addr.ip4.addr = esp_ip4addr_aton(CONFIG_NODE_DNS_FALLBACK);
            dns.ip.type = ESP_IPADDR_TYPE_V4;
            offer_dns_to_clients(&dns);

            /* An AP-side interface is up as soon as the module beacons. */
            hgic_netif_set_link(s_halow_netif, true);
            err = esp_netif_napt_enable(s_halow_netif);
            printf("NAT %s on the halow subnet\n", err == ESP_OK ? "enabled" : "ENABLE FAILED");
        }
        else
        {
#if CONFIG_NODE_HALOW_USE_DHCP
            ESP_ERROR_CHECK(hgic_netif_use_dhcp(s_halow_netif));
#else
            ESP_ERROR_CHECK(hgic_netif_set_static_ip(s_halow_netif, CONFIG_NODE_HALOW_STATIC_IP,
                                                     CONFIG_NODE_HALOW_STATIC_NETMASK,
                                                     CONFIG_NODE_HALOW_STATIC_GW));
#endif
        }
    }

    wifi_start();

    if (s_halow_netif != nullptr && !s_gateway)
    {
        esp_netif_set_default_netif(s_halow_netif);
    }

    ESP_ERROR_CHECK(at_uart_init());
    if (!at_uart_resync())
    {
        printf("at_uart_resync: AT console not responding\n");
    }
    at_uart_quiet();
    ESP_ERROR_CHECK(web_server_start());

    const httpd_uri_t api_status = {
        .uri = "/api/status", .method = HTTP_GET, .handler = status_get_handler, .user_ctx = NULL};
    httpd_register_uri_handler(web_server_handle(), &api_status);

    if (halow_ok)
    {
        xTaskCreatePinnedToCore(halow_pump_task, "halow_pump", 6144, NULL, 15, NULL, 1);
    }

#if CONFIG_NODE_CAMERA_STREAM
    /* Field unit only. The gateway sits indoors routing traffic and has no
     * camera module; bringing the CSI/ISP/H.264 stack up there costs it the
     * internal DMA heap that esp_hosted's SDIO uplink needs, which the stall
     * watchdog answers with a host restart. Kept before the rollback cancel
     * on purpose: if camera bring-up wedges a board, its next boot is the
     * previous image. */
    if (!s_gateway)
    {
        camera_stream_start();
    }
#endif

    /* Portal is serving: this image can always be OTA'd/rebooted from here. */
    esp_ota_mark_app_valid_cancel_rollback();

    vTaskSuspend(NULL);
}
