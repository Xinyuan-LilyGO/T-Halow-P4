# HaLow router — WiFi-to-HaLow NAT router with a web configurator

Turns the T-Halow-P4 into a self-contained, pocketable router: phones and
laptops join the onboard ESP32-C6's WiFi SoftAP and their traffic is NAT'd
onto the Wi-Fi HaLow link. A configuration page served by the board itself at
**http://192.168.4.1/** drives the radio's AT console from any browser — no
USB, no Web Serial, no app.

```
phone/laptop ──WiFi──> C6 SoftAP ──SDIO──> ESP32-P4 [lwIP NAPT] ──SPI──> HaLow STA ──sub-GHz──> HaLow AP
```

## How it works

- **WiFi side** — the ESP32-C6 runs as a SoftAP through
  [`esp_hosted`](https://components.espressif.com/components/espressif/esp_hosted) /
  [`esp_wifi_remote`](https://components.espressif.com/components/espressif/esp_wifi_remote)
  over SDIO, the same arrangement as `examples/factory_no_screen`. The ESP32-P4
  runs a DHCP server on the SoftAP subnet (192.168.4.0/24).
- **HaLow side** — the TX-AH-R900P module is driven over 40 MHz SPI and exposed
  to lwIP as an ethernet-like `esp_netif` by the `hgic_netif` component from
  [`examples/halow_netif`](../halow_netif) (shared via `EXTRA_COMPONENT_DIRS`,
  not copied). The router's HaLow role is always **station**; it associates to
  whatever HaLow AP the radio is configured for.
- **Routing** — lwIP NAPT (`CONFIG_LWIP_IPV4_NAPT`) translates the SoftAP
  subnet onto the HaLow address, so any number of WiFi clients share the link.
- **Config page** — the firmware embeds the
  [thalow-config](https://github.com/GlassOnTin/thalow-config) configurator
  (gzipped at build time) and bridges `POST /api/at` onto the module's AT UART.
  The page shows firmware/role/SSID/channel status, live RSSI, and applies
  SSID / frequency / bandwidth / key / TX-power changes. The radio stores those
  settings in its own flash, so they survive reboots. A raw AT console is
  included for anything else.

## Defaults

| Setting | Default | Where |
|---|---|---|
| WiFi SSID / password | `halow-router` / `halowrouter` | menuconfig → *T-Halow-P4 HaLow router example* |
| WiFi channel | 1 | menuconfig |
| Router address | 192.168.4.1 (DHCP server on) | fixed |
| HaLow IP | static 10.99.0.2/24, gw 10.99.0.1 | menuconfig (or DHCP) |
| DNS offered to clients | 8.8.8.8 (static) / learned lease (DHCP) | menuconfig |
| HaLow SSID/freq/BW/key | whatever the radio has stored | config page |

## Using it from a phone

1. Power the board (battery or USB). Join the `halow-router` WiFi network.
2. Browse to **http://192.168.4.1/**. The page connects automatically and shows
   the radio's current state.
3. Set SSID, centre frequency (×0.1 MHz, e.g. `8660` = 866.0 MHz), bandwidth
   and PSK to match your HaLow AP, then **Apply**. The status table and RSSI
   trace confirm association.
4. Traffic now flows: the phone reaches whatever is behind the HaLow AP.

After a **factory reset** (`AT+LOADDEF=1`), reboot the board: the radio reverts
to its default role on reset and the firmware re-asserts station mode at boot.

## Build and flash

Requires ESP-IDF v5.4.1 or later.

```
idf.py set-target esp32p4
idf.py menuconfig        # optional: WiFi credentials, HaLow addressing
idf.py build flash monitor
```

DTR/RTS are not wired to EN/BOOT on this board, so esptool cannot reset it
automatically: hold **BOOT**, tap **RST**, release **BOOT** to enter the
bootloader, then tap **RST** again after flashing to run the new image.

## Measured

Bench: this board as the HaLow **station** running `halow_router`; a second
T-Halow board as the HaLow **AP**; both on the desk (strong signal), 866.0 MHz /
2 MHz bandwidth, open. A laptop joined the C6 SoftAP and a host on the AP's far
side terminated the traffic. Short range only — range and RSSI-vs-rate were not
characterised. Downlink measured with TCP (bulk); uplink capacity with UDP at a
fixed offered rate (avoids TCP congestion collapse masking the link limit).

| Path | Direction | Offered | Result |
|---|---|---|---|
| ICMP through NAT | round-trip | — | 0% loss, ~24 ms RTT, TTL decremented (ttl=63) |
| TCP bulk | downlink (HaLow→client) | — | **2.22 Mbit/s** sustained, clean |
| UDP | uplink (client→HaLow) | 0.5 Mbit/s | 0% loss |
| UDP | uplink | 1.0 Mbit/s | 0% loss |
| UDP | uplink | 2.0 Mbit/s | ~99% loss (link saturated) |
| UDP | uplink | 4.0 Mbit/s | 100% loss; C6 SoftAP dropped — see Limitations |

The link is **asymmetric**: downlink (P4 receiving on HaLow) sustains ~2.2 Mbit/s,
while uplink (P4 transmitting over the SPI→HaLow path) is clean to ~1 Mbit/s and
saturates by 2. For an uplink-heavy workload (e.g. a camera streaming home), pace
the source to ≤1 Mbit/s.

## Limitations

- WiFi credentials are compile-time (menuconfig); changing them means
  reflashing.
- The config page and `/api/at` are unauthenticated — the same trust model as
  the board's USB console, moved onto its private SoftAP. Set a WiFi password.
- If the HaLow AP's far side uses 192.168.4.0/24, NAT will not work; change one
  of the subnets.
- Throughput is **asymmetric** and bounded by the HaLow link: ~2.2 Mbit/s
  downlink, ~1 Mbit/s clean uplink at 2 MHz bandwidth (see Measured). Ample for
  sensors, telemetry, SSH and modest-rate cameras; not for high-bitrate video.
- **Uplink overload:** offering well past the uplink's capacity (≳2 Mbit/s) can
  exhaust `esp_hosted`'s SDIO receive buffers — with the stock component this
  resets the board, and in any case the C6 SoftAP may drop and need a
  power-cycle to recover. The P4 does not (yet) apply backpressure toward the
  C6, so pace uplink sources to the link capacity.
- The WPA-PSK path follows the vendor reference but has not been verified on
  hardware.

---

## 中文简介

本示例把 T-Halow-P4 变成一台独立的「口袋 HaLow 路由器」：手机/电脑加入板载
ESP32-C6 的 WiFi 热点（默认 `halow-router` / `halowrouter`），流量经 lwIP NAPT
转发到 HaLow 链路。浏览器打开 **http://192.168.4.1/** 即可配置模块（SSID、频率、
带宽、密钥、发射功率），页面由开发板自身提供，无需 USB 或任何应用。模块的
HaLow 角色固定为 STA；配置保存在模块自身的 flash 中。烧录方法与其他示例相同
（按住 BOOT、点按 RST 进入下载模式，烧录后再点按 RST 运行）。
