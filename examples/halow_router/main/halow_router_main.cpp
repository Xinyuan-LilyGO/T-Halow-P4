/*
 * WiFi-to-HaLow NAT router on the T-Halow-P4.
 *
 * Clients join the onboard ESP32-C6's SoftAP and are NAT'd onto the Wi-Fi
 * HaLow link, and a config page served at http://192.168.4.1/ drives the
 * radio's AT console from any browser:
 *
 *   client ──WiFi──> C6 SoftAP ──SDIO──> ESP32-P4 [lwIP NAPT] ──SPI──> HaLow STA ──> HaLow AP
 *
 * Derived from examples/halow_netif (HaLow side) and examples/factory_no_screen
 * (C6 WiFi via esp_hosted/esp_wifi_remote).
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
#include "esp_timer.h"
#include "esp_wifi_remote.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "t_halow_p4_config.h"
#include "cpp_bus_driver_library.h"
#include "hgic_netif.h"

#include "at_uart.h"
#include "web_server.h"

extern "C"
{
#include "hgic_sdspi.h"
#include "hgic_raw.h"
}

/*
 * Connect HaLow egress backpressure to esp_hosted's uplink flow control: when
 * the radio TX queue in hgic_netif backs up, pause the C6's Wi-Fi RX so 802.11
 * flow control reaches the WiFi client. Overrides the weak no-op in hgic_netif;
 * esp_hosted_wifi_rx_throttle() lives in the (modified) esp_hosted host driver.
 */
extern "C" void esp_hosted_wifi_rx_throttle(bool pause);
extern "C" void hgic_netif_egress_throttle(bool pause)
{
    esp_hosted_wifi_rx_throttle(pause);
}

#define MAX_SPI_RECEIVE_SIZE std::min(HGIC_RAW_DATA_ROOM, HGIC_RAW_MAX_PAYLOAD)
#define HGIC_RX_BUF_SIZE 2048

auto spidrv_read_buffer = std::make_unique<uint8_t[]>(MAX_SPI_RECEIVE_SIZE);

volatile bool Interrupt_Flag = false;

static esp_netif_t *s_halow_netif = nullptr;
static esp_netif_t *s_ap_netif = nullptr;

// Diagnostics (temporary): count SPI frames pumped and data frames handed to
// lwIP, so a bench run can tell "nothing arrives over HaLow" from "arrives but
// isn't routed".
static volatile uint32_t s_rx_frames = 0;
static volatile uint32_t s_rx_data_frames = 0;

auto Tx_Ah_R900pnr_Spi_Bus = std::make_shared<Cpp_Bus_Driver::Hardware_Spi>(
    TX_AH_R900PNR_MOSI, TX_AH_R900PNR_SCLK, TX_AH_R900PNR_MISO,
    SPI2_HOST, 0, spi_clock_source_t::SPI_CLK_SRC_SPLL);

// ---------------------------------------------------------------------------
// SPI glue required by the vendor driver.
// raw_send() is renamed to hgic_spi_raw_send() by a compile definition on the
// vendor component, because lwIP exports a raw_send() of its own.
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

void spidrv_cs(void *priv, char enable)
{
    // CS is asserted by the SPI peripheral for the duration of each transfer.
}

int spidrv_hw_crc(void *priv, unsigned char *data, unsigned int len, char flag)
{
    return 0;
}

int raw_send(unsigned char *data, unsigned int len)
{
    return hgic_sdspi_write(0, data, len);
}

// ---------------------------------------------------------------------------

// Drain the module's SPI queue. The interrupt is edge-triggered on FALLING, so
// reading only once per interrupt strands any frame that arrived while the
// previous one was being serviced: the line is already low, so no further edge
// is generated and the queue stalls.
static void hgic_pump_rx(void)
{
    static auto rx = std::make_unique<unsigned char[]>(HGIC_RX_BUF_SIZE);

    for (int i = 0; i < 8; i++)
    {
        // Hold the SPI lock only across the bus read and the vendor dispatch --
        // both touch shared hgic state that TX also uses. rx stays valid until
        // the next locked read overwrites it, so the stack push below is done
        // unlocked (esp_netif_receive can block on the tcpip mailbox, and TX may
        // be waiting on this lock from the tcpip thread).
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

        // hgic_raw_rx() dispatches command responses and events internally, and
        // for data it advances p past the hgic header onto the ethernet frame.
        int type = hgic_raw_rx(&p, &len);
        hgic_netif_spi_unlock();

        if (type == HGIC_RAW_RX_TYPE_DATA && s_halow_netif != nullptr)
        {
            s_rx_data_frames++;
            hgic_netif_input(s_halow_netif, p, len);
        }
    }
}

// hgic_raw_get_fwinfo() only writes the request; hgic.addr is populated later,
// when hgic_raw_rx() consumes the response. Pump RX until the MAC turns up.
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
// WiFi SoftAP (the onboard ESP32-C6 over SDIO, via esp_hosted/esp_wifi_remote)
// and NAT onto the HaLow netif.
// ---------------------------------------------------------------------------

// DHCP-server option flag for "offer DNS to clients"; not exported by a public
// IDF header (the softap_sta example defines it the same way).
#define DHCPS_OFFER_DNS 0x02

// Push the router's DNS server to SoftAP clients via the DHCP server. The
// option can only be set while the DHCP server is stopped.
static void offer_dns_to_clients(const esp_netif_dns_info_t *dns)
{
    uint8_t dhcps_offer_option = DHCPS_OFFER_DNS;
    esp_netif_dhcps_stop(s_ap_netif);
    ESP_ERROR_CHECK(esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
                                           &dhcps_offer_option, sizeof(dhcps_offer_option)));
    ESP_ERROR_CHECK(esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, (esp_netif_dns_info_t *)dns));
    esp_netif_dhcps_start(s_ap_netif);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START)
    {
        // The AP netif only comes up when this event is processed -- with
        // esp_wifi_remote that is well after esp_wifi_start() returns, and
        // esp_netif_napt_enable() fails on a down netif. So NAT and the DHCP
        // DNS option are configured here, not in app_main().
        esp_netif_dns_info_t dns = {};
#if CONFIG_HALOW_USE_DHCP
        // Offer whatever the HaLow lease has provided so far; re-offered from
        // IP_EVENT_ETH_GOT_IP when the lease (or a renewal) arrives.
        if (s_halow_netif != nullptr)
        {
            esp_netif_get_dns_info(s_halow_netif, ESP_NETIF_DNS_MAIN, &dns);
        }
#else
        dns.ip.u_addr.ip4.addr = esp_ip4addr_aton(CONFIG_HALOW_DNS);
        dns.ip.type = ESP_IPADDR_TYPE_V4;
#endif
        if (dns.ip.u_addr.ip4.addr != 0)
        {
            offer_dns_to_clients(&dns);
        }

        esp_err_t err = esp_netif_napt_enable(s_ap_netif);
        printf("NAT %s on the softap subnet\n", err == ESP_OK ? "enabled" : "ENABLE FAILED");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        printf("wifi client " MACSTR " joined\n", MAC2STR(event->mac));
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        printf("wifi client " MACSTR " left\n", MAC2STR(event->mac));
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP)
    {
        // The hgic netif is ethernet-inherent, so its DHCP lease arrives as an
        // ETH event. Learn the upstream DNS and re-offer it to WiFi clients.
        esp_netif_dns_info_t dns;
        if (esp_netif_get_dns_info(s_halow_netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK &&
            dns.ip.u_addr.ip4.addr != 0)
        {
            offer_dns_to_clients(&dns);
        }
    }
}

static void wifi_ap_start(void)
{
    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));

    wifi_config_t ap_config = {};
    strlcpy((char *)ap_config.ap.ssid, CONFIG_ROUTER_WIFI_SSID, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(CONFIG_ROUTER_WIFI_SSID);
    strlcpy((char *)ap_config.ap.password, CONFIG_ROUTER_WIFI_PASSWORD, sizeof(ap_config.ap.password));
    ap_config.ap.channel = CONFIG_ROUTER_WIFI_CHANNEL;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = strlen(CONFIG_ROUTER_WIFI_PASSWORD) > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    printf("wifi softap \"%s\" up, config page on http://192.168.4.1/\n", CONFIG_ROUTER_WIFI_SSID);
}

// The HaLow SPI service: drain RX on interrupt, track the link, refresh the
// connection state. Runs on core 1 (pinned by app_main) so the WiFi/SDIO work
// on core 0 cannot starve the SPI drain.
static void halow_pump_task(void *arg)
{
    // Seed hgic.status.conn_state; the response is parsed by hgic_raw_rx().
    hgic_netif_spi_lock();
    hgic_raw_get_connect_state();
    hgic_netif_spi_unlock();

    int last_link = -1;
    int64_t next_poll = 0;
    int64_t next_stats = 0;

    while (1)
    {
        hgic_netif_spi_lock();
        int alive = hgic_sdspi_detect_alive(0);
        if (alive == -1)
        {
            hgic_sdspi_init(0);
        }
        hgic_netif_spi_unlock();
        if (alive == -1)
        {
            printf("hgic_sdspi_detect_alive fail, re-initialising\n");
        }

        if (Interrupt_Flag)
        {
            Interrupt_Flag = false;
            hgic_pump_rx();
        }

        int64_t now = esp_timer_get_time() / 1000;

        // Refresh the connection state periodically. The module does not always
        // push an async event when it associates after boot, so relying on a
        // single seed query can leave the netif link stuck down.
        if (now >= next_poll)
        {
            next_poll = now + 1000;
            hgic_netif_spi_lock();
            hgic_raw_get_connect_state();
            hgic_netif_spi_unlock();
        }

        if (hgic.status.conn_state != last_link)
        {
            last_link = hgic.status.conn_state;
            printf("halow link %s (conn_state=%d)\n", last_link != 0 ? "UP" : "DOWN", last_link);
            hgic_netif_set_link(s_halow_netif, last_link != 0);
        }

        if (now >= next_stats)
        {
            next_stats = now + 3000;
            printf("halow rx: frames=%lu data=%lu\n",
                   (unsigned long)s_rx_frames, (unsigned long)s_rx_data_frames);
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

extern "C" void app_main(void)
{
    // NVS backs the web-configured HaLow role (and esp_wifi_init() needs it
    // too), so bring it up before the radio.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    Tx_Ah_R900pnr_Spi_Bus->create_gpio_interrupt(
        TX_AH_R900PNR_INT, Cpp_Bus_Driver::Tool::Interrupt_Mode::FALLING,
        [](void *arg) -> IRAM_ATTR void
        {
            Interrupt_Flag = true;
        });

    memset(spidrv_read_buffer.get(), 0xFF, MAX_SPI_RECEIVE_SIZE);

    // The TX-AH module requires a 40 MHz SPI clock.
    Tx_Ah_R900pnr_Spi_Bus->begin(40000000, TX_AH_R900PNR_CS);

    // Give the module's SPI slave time to come up.
    vTaskDelay(pdMS_TO_TICKS(1000));

    while (hgic_sdspi_init(0) == -1)
    {
        printf("hgic_sdspi_init fail, retrying\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    printf("hgic_sdspi_init success\n");

    // Unlike halow_netif, a dead radio is not fatal here: bring the AP and the
    // config page up anyway so the module can be diagnosed from a browser.
    bool halow_ok = hgic_wait_for_mac(5000);
    if (halow_ok)
    {
        // The module persists ssid/channel/bandwidth/key itself, but reverts
        // the role on power-up, so re-assert one every boot: "sta" (the normal
        // router topology -- join a HaLow AP that bridges to the internet
        // gateway) unless the config page has set AT+MODE=ap, which the web
        // server remembers in NVS.
        char role[8] = "sta";
        web_server_get_halow_role(role, sizeof(role));
        printf("halow role: %s\n", role);
        hgic_raw_set_mode(role);
    }
    else
    {
        printf("hgic_raw_get_fwinfo: no response, HaLow uplink disabled\n");
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if (halow_ok)
    {
        s_halow_netif = hgic_netif_create(hgic.addr);
        if (s_halow_netif == nullptr)
        {
            printf("hgic_netif_create fail\n");
            return;
        }

#if CONFIG_HALOW_USE_DHCP
        ESP_ERROR_CHECK(hgic_netif_use_dhcp(s_halow_netif));
#else
        ESP_ERROR_CHECK(hgic_netif_set_static_ip(s_halow_netif, CONFIG_HALOW_STATIC_IP,
                                                 CONFIG_HALOW_STATIC_NETMASK,
                                                 CONFIG_HALOW_STATIC_GW));
        esp_netif_dns_info_t dns = {};
        dns.ip.u_addr.ip4.addr = esp_ip4addr_aton(CONFIG_HALOW_DNS);
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        ESP_ERROR_CHECK(esp_netif_set_dns_info(s_halow_netif, ESP_NETIF_DNS_MAIN, &dns));
#endif
    }

    wifi_ap_start();
    // NAT and the client DNS offer happen in the WIFI_EVENT_AP_START handler,
    // once the AP netif is actually up.

    if (s_halow_netif != nullptr)
    {
        esp_netif_set_default_netif(s_halow_netif);
    }

    // The AT UART is independent of the SPI data path, so the config page can
    // talk to the radio even when the SPI bring-up failed.
    ESP_ERROR_CHECK(at_uart_init());
    if (!at_uart_resync())
    {
        printf("at_uart_resync: AT console not responding\n");
    }
    at_uart_quiet();
    ESP_ERROR_CHECK(web_server_start());

    if (halow_ok)
    {
        // Run the HaLow SPI service on core 1, away from the WiFi/SDIO tasks on
        // core 0. The 40 MHz SPI to the radio and the C6's SDIO both want the
        // bus and the CPU; sharing a core starves the SPI drain and wedges the
        // module.
        printf("halow uplink active: netif=%p, starting pump task on core 1\n", (void *)s_halow_netif);
        xTaskCreatePinnedToCore(halow_pump_task, "halow_pump", 6144, NULL, 15, NULL, 1);
    }
    else
    {
        // The web server and AT bridge run on their own tasks; nothing to pump.
        printf("halow uplink NOT active (module MAC never read); web config only\n");
    }

    // Do not return from app_main: on this target, self-deleting the main task
    // faults the idle task as it reclaims the stack. Keep it parked instead.
    vTaskSuspend(NULL);
}
