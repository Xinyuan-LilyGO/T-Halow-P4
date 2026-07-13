# halow_netif

IP networking over Wi-Fi HaLow on the T-Halow-P4.

This example brings the Taixin TX-AH-R900PNR module up as an `esp_netif` interface, so the
ESP32-P4 can use the normal ESP-IDF socket API — ping, UDP, TCP, iperf — over the sub-GHz radio.
The AP-side module (for example a T-Halow RJ45 board) bridges the BSS onto ethernet at layer 2,
so frames sent from the P4 appear directly on that wire.

## How it works

The vendor driver already presents a complete ethernet frame on both edges:

```c
hgic_raw_send_ether(frame, len);                    /* TX */
hgic_raw_rx(&frame, &len) == HGIC_RAW_RX_TYPE_DATA  /* RX */
```

`components/hgic_netif` hangs those two calls off an `esp_netif` driver, the same way
`esp-hosted` does. The vendor's `demo/hgic_lwip_demo.c` is **not** used — see below.

## Configuration

The radio's SSID, channel, bandwidth and encryption are set over the **AT console**, not by this
firmware. The USB-C port exposes a CH343 at 115200 baud, and `at_bridge_task()` forwards it to
the module's AT UART, so AT commands work while this firmware runs.

Bring up an AP on another board and a station here, for example:

```
AT+LOADDEF=1        # both ends, then reconfigure — association fails without this
AT+MODE=ap          # or sta
AT+SSID=halowbench
AT+KEYMGMT=NONE
AT+CHAN_LIST=8660   # 866.0 MHz
AT+BSS_BW=2         # MHz
AT+TXPOWER=6        # dBm; turn this down for bench testing at close range
```

Addressing is under `idf.py menuconfig` → *T-Halow-P4 HaLow netif example*. It defaults to a
static `10.99.0.2/24`, because a bare HaLow BSS has no DHCP server.

## Build and flash

Requires ESP-IDF >= 5.4.1.

```bash
idf.py set-target esp32p4
idf.py build
```

DTR/RTS are not wired to EN/BOOT on this board, so esptool cannot reset it. To flash: hold
**BOOT**, tap **RST**, release RST, release BOOT, then `idf.py -p PORT flash`. Tap **RST** again
afterwards to run the new image — `--after hard_reset` has no effect.

## Measured

Two boards 1 m apart, 866.0 MHz, 2 MHz bandwidth, open, 6 dBm, ESP-IDF v5.4.1:

```
400 packets transmitted, 400 received, 0% packet loss
rtt min/avg/max/mdev = 13.039/27.516/40.649/4.280 ms
~1.28 Mbps each way (2.55 Mbps aggregate)
```

1500-byte MTU passes with DF set. Twenty seconds of sustained load produced no
`sdio dev tx timeout` and no dropped frames.

## Notes on the vendor driver

Three things were found while writing this, all of which affect anyone using the SPI path:

1. **`raw_send()` collides with lwIP.** The driver requires the application to export a global
   `raw_send()`. lwIP exports one too (`lwip/src/core/raw.c`), and `LWIP_RAW` is hard-wired to 1
   in ESP-IDF with no Kconfig. As soon as any netif is linked the build fails with
   `multiple definition of 'raw_send'`. The vendor component's `CMakeLists.txt` now renames the
   hook with `target_compile_definitions(... PUBLIC raw_send=hgic_spi_raw_send)`, which touches
   no vendor source and leaves `halow_spi_single` byte-for-byte identical. This is also why
   `hgic_lwip_demo.c` could never have linked.

2. **`hgic_raw_get_fwinfo()` is asynchronous.** `hgic_raw_send_cmd()` writes the request and
   returns; `hgic.addr` is populated later, inside `hgic_raw_rx()`, when the response arrives.
   Reading the MAC straight after the call yields `00:00:00:00:00:00`. `hgic_wait_for_mac()`
   pumps RX until a real address appears.

3. **The module interrupt is edge-triggered (FALLING).** Reading one buffer per interrupt
   strands frames that arrive while the previous one is being serviced — the line is already low,
   so no further edge is generated. `hgic_pump_rx()` drains until the read returns empty.

`hgic_lwip_demo.c` additionally does not compile (stray comma at line 52) and treats
`hgic_raw_send_ether()`'s positive return value — the number of bytes written — as an error.

## Shared components

`cpp_bus_driver`, `huge-ic-nonos-driver-*` and `private_library` are taken from
`examples/halow_spi_single/components` via `EXTRA_COMPONENT_DIRS` rather than duplicated, to keep
the diff reviewable.
