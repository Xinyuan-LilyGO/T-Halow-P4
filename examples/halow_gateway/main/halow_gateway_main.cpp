/*
 * HaLow gateway on the T-Halow-P4: the routed replacement for a transparent
 * RJ45 bridge at the infrastructure end of a HaLow link.
 *
 *   home LAN ──WiFi──> C6 STA ──SDIO──> ESP32-P4 [lwIP NAPT] ──SPI──> HaLow AP ))) camera STAs
 *
 * The HaLow module runs as the AP; the P4 serves DHCP on the HaLow BSS and
 * masquerades clients out over the home WiFi. Because this end ROUTES instead
 * of bridging, the LAN's broadcast/multicast chatter never reaches the air --
 * the airtime and battery cost a bridged AP imposes on every HaLow client
 * simply does not exist here (no vendor mcast_filter needed).
 *
 * The config page is served at the C6's WiFi address (from the home LAN) and
 * at GW_HALOW_IP (from the HaLow side); /api/reboot and /api/ota work as in
 * examples/halow_router, so after one serial flash the board is managed
 * entirely over the network.
 *
 * Derived from examples/halow_router. License: GPL 3.0
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
#include "web_server.h"

extern "C"
{
#include "hgic_sdspi.h"
#include "hgic_raw.h"
}

/*
 * Connect HaLow egress backpressure to esp_hosted's flow control: when the
 * radio TX queue in hgic_netif backs up (heavy WiFi->HaLow forwarding), pause
 * the C6's RX so 802.11 flow control reaches the sender. Overrides the weak
 * no-op in hgic_netif.
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
static esp_netif_t *s_sta_netif = nullptr;

static volatile uint32_t s_rx_frames = 0;
static volatile uint32_t s_rx_data_frames = 0;
static volatile uint32_t s_rx_chatter_dropped = 0;

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

/*
 * Drop group-addressed frames arriving from the HaLow side except ARP and
 * IPv4 broadcast DHCP (clients DISCOVER/REQUEST by broadcast -- the DHCP
 * server needs those). Same reasoning as the router example, pointed the
 * other way: NAT never forwards them, so don't pay lwIP for the discard.
 */
static bool halow_chatter(const uint8_t *frame, size_t len)
{
    if (len < 14 || (frame[0] & 1) == 0)
    {
        return false; /* unicast (or runt): keep */
    }
    uint16_t ethertype = (frame[12] << 8) | frame[13];
    if (ethertype == 0x0806)
    {
        return false; /* ARP: keep */
    }
    if (ethertype == 0x0800 && len >= 14 + 20 + 8)
    {
        size_t ihl = (frame[14] & 0x0f) * 4;
        if (frame[23] == 17 && len >= 14 + ihl + 8)
        {
            uint16_t dport = (frame[14 + ihl + 2] << 8) | frame[14 + ihl + 3];
            if (dport == 67 || dport == 68)
            {
                return false; /* DHCP: keep */
            }
        }
    }
    return true;
}

// Drain the module's SPI queue. The interrupt is edge-triggered on FALLING, so
// reading only once per interrupt strands any frame that arrived while the
// previous one was being serviced.
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
// WiFi station on the C6 (esp_hosted/esp_wifi_remote) + DNS relay to the
// HaLow DHCP server + NAPT.
// ---------------------------------------------------------------------------

#define DHCPS_OFFER_DNS 0x02

// Hand HaLow DHCP clients an upstream DNS. The option can only be set while
// the DHCP server is stopped.
static void offer_dns_to_halow(const esp_netif_dns_info_t *dns)
{
    uint8_t dhcps_offer_option = DHCPS_OFFER_DNS;
    esp_netif_dhcps_stop(s_halow_netif);
    ESP_ERROR_CHECK(esp_netif_dhcps_option(s_halow_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
                                           &dhcps_offer_option, sizeof(dhcps_offer_option)));
    ESP_ERROR_CHECK(esp_netif_set_dns_info(s_halow_netif, ESP_NETIF_DNS_MAIN, (esp_netif_dns_info_t *)dns));
    esp_netif_dhcps_start(s_halow_netif);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        printf("wifi disconnected, retrying\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)event_data;
        printf("wifi ip " IPSTR " gw " IPSTR "\n", IP2STR(&e->ip_info.ip), IP2STR(&e->ip_info.gw));

        // Follow the home network's DNS; until this fires the fallback from
        // Kconfig (set in app_main) is offered instead.
        esp_netif_dns_info_t dns;
        if (esp_netif_get_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK &&
            dns.ip.u_addr.ip4.addr != 0)
        {
            offer_dns_to_halow(&dns);
        }
    }
}

static void wifi_sta_start(void)
{
    s_sta_netif = esp_netif_create_default_wifi_sta();
    assert(s_sta_netif != nullptr);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));

    wifi_config_t sta_config = {};
    strlcpy((char *)sta_config.sta.ssid, CONFIG_GW_WIFI_STA_SSID, sizeof(sta_config.sta.ssid));
    strlcpy((char *)sta_config.sta.password, CONFIG_GW_WIFI_STA_PASSWORD, sizeof(sta_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    printf("wifi sta joining \"%s\"\n", CONFIG_GW_WIFI_STA_SSID);
}

// The HaLow SPI service: drain RX on interrupt, keep the module state fresh.
// Runs on core 1 so the WiFi/SDIO work on core 0 cannot starve the SPI drain.
static void halow_pump_task(void *arg)
{
    hgic_netif_spi_lock();
    hgic_raw_get_connect_state();
    hgic_netif_spi_unlock();

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
            hgic_raw_get_connect_state(); // feeds rssi/sta state for diagnostics
            hgic_netif_spi_unlock();
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

    // A dead radio is not fatal: bring WiFi and the config page up anyway so
    // the module can be diagnosed from a browser.
    bool halow_ok = hgic_wait_for_mac(5000);
    if (halow_ok)
    {
        // This end is the AP by definition -- but stay overridable from the
        // config page (AT+MODE persists via NVS), which turns the same image
        // into a plain station for bench experiments.
        char role[8] = "ap";
        web_server_get_halow_role(role, sizeof(role));
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
        s_halow_netif = hgic_netif_create_dhcps(hgic.addr, CONFIG_GW_HALOW_IP,
                                                CONFIG_GW_HALOW_NETMASK);
        if (s_halow_netif == nullptr)
        {
            printf("hgic_netif_create_dhcps fail\n");
            return;
        }

        // Fallback DNS until the WiFi lease supplies the real one.
        esp_netif_dns_info_t dns = {};
        dns.ip.u_addr.ip4.addr = esp_ip4addr_aton(CONFIG_GW_DNS_FALLBACK);
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        offer_dns_to_halow(&dns);

        // An AP-side interface is up as soon as the module beacons; there is
        // no association event to wait for.
        hgic_netif_set_link(s_halow_netif, true);

        err = esp_netif_napt_enable(s_halow_netif);
        printf("NAT %s on the halow subnet\n", err == ESP_OK ? "enabled" : "ENABLE FAILED");
    }

    wifi_sta_start();

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
        printf("halow gateway active: dhcps on %s, starting pump task on core 1\n", CONFIG_GW_HALOW_IP);
        xTaskCreatePinnedToCore(halow_pump_task, "halow_pump", 6144, NULL, 15, NULL, 1);
    }

    // The web portal is up, so a remote OTA/reboot can always recover the
    // board from here: accept this image. An OTA image that dies before this
    // line is rolled back by the bootloader on the next reset.
    esp_ota_mark_app_valid_cancel_rollback();

    // Do not return from app_main: on this target, self-deleting the main task
    // faults the idle task as it reclaims the stack. Keep it parked instead.
    vTaskSuspend(NULL);
}
