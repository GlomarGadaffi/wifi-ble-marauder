# wifi-ble-marauder

Pure ESP-IDF 6.0.1 WiFi/BLE security research tool. No Arduino. No wrappers.

Successor to [ESP32Marauder](https://github.com/justcallmekoko/ESP32Marauder) — rewritten from the ground up in C against the native IDF stack, with new attack vectors that Arduino cannot touch.

## What's new vs. Marauder

| Capability | Marauder (Arduino) | wifi-ble-marauder (IDF 6.0.1) |
|---|---|---|
| 5 GHz deauth + beacon spam | ✗ | ✓ ESP32-C5 |
| WiFi 6 (802.11ax) frame crafting | ✗ | ✓ C5/C6 |
| Live RSSI spectrum on touch display | ✗ | ✓ S3 + Guition |
| Thread neighbor topology dump | ✗ | ✓ C6 (Thread 1.3 certified) |
| Matter commissioning intercept | ✗ | ✓ C6 |
| Zigbee network enumeration | ✗ | ✓ C6 |
| BLE 5.0 extended adv (254-byte) | ✗ (31-byte limit) | ✓ C5/C6 |
| 802.15.4 + WiFi collision DoS | ✗ | ✓ C5/C6 |
| Multi-core task isolation | ✗ (single Arduino loop) | ✓ attack core 0, UI/CLI core 1 |
| µs-precision EAPOL timing | ✗ | ✓ esp_timer |
| Multi-radio interleaved PCAP | ✗ | ✓ WiFi + BLE + 802.15.4 |

## Hardware targets

### Active — S3 + Guition display (Phase 4)
**[Guition JC3248W535EN](https://github.com/GlomarGadaffi/jc3248-display-driver)**
- ESP32-S3, 240 MHz, 8 MB OPI PSRAM, 16 MB flash
- AXS15231B QSPI 320×480 IPS display, FT5336 capacitive touch
- Display driver: [jc3248-display-driver](https://github.com/GlomarGadaffi/jc3248-display-driver)

```bash
idf.py --sdkconfig-defaults sdkconfig.defaults.s3_guition set-target esp32s3 build
idf.py -p COM3 flash monitor
```

On boot the display shows a live 2.4 GHz RSSI spectrum. Touch a channel bar to select it as the active attack channel, then tap an action button:

```
┌──────────────────────────────────────┐
│ wifi-ble-marauder   CH:6   2.4 GHz RSSI   │  header
├──────────────────────────────────────┤
│  ▁▃█▅▁▁▂▁▁▁▃▁▁                      │  channel bars 1–13
│  1 2 3 4 5 6 7 8 9 10 11 12 13      │  tap to select
├──────────────────────────────────────┤
│  ch 6  -62dBm  CoffeeShop           │  recent APs
│  ch 1  -78dBm  NETGEAR_5A           │
├──────────────────────────────────────┤
│ [DEAUTH] [BEACON] [SWEEP]  [STOP]   │  action bar
└──────────────────────────────────────┘
```

UART CLI still works in parallel at 115200 baud.

### Phase 2 — C5 (5 GHz attacks)
**[Espressif ESP32-C5-DevKitC-1-N8R4](https://www.espressif.com/en/products/devkits)**
- ESP32-C5, 240 MHz RISC-V, dual-band 2.4 + 5 GHz WiFi 6 (802.11ax)
- BLE 5.0, 8 MB Flash, 4 MB PSRAM, native USB-CDC

```bash
idf.py --sdkconfig-defaults sdkconfig.defaults.c5 set-target esp32c5 build flash monitor
```

### Phase 3 — C6 (Thread / Matter / Zigbee)
**[Seeed XIAO ESP32-C6](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/)**
- ESP32-C6, Thread 1.3 / Zigbee 3.x certified, WiFi 6, BLE 5.3

```bash
idf.py --sdkconfig-defaults sdkconfig.defaults.c6 set-target esp32c6 build flash monitor
```

## Project structure

```
wifi-ble-marauder/
├── main/main.c                    # app_main — board dispatch + task spawn
├── components/
│   ├── wifi_attack/               # 802.11 frame inject/sniff (native esp_wifi)
│   ├── ble_attack/                # NimBLE attacks (esp_nimble direct)
│   ├── cli/                       # esp_console REPL (UART, all boards)
│   ├── display/                   # S3 Guition: backlight, QSPI panel, I2C touch, framebuf
│   ├── spectrum/                  # S3 Guition: RSSI spectrum task + touch dispatch
│   ├── esp_lcd_axs15231b/         # AXS15231B QSPI + touch driver (from jc3248-display-driver)
│   ├── pcap/                      # SD-backed PCAP writer with radiotap
│   ├── evil_portal/               # captive portal (esp_http_server + DNS)
│   ├── gps/                       # NMEA UART parser
│   └── board/                     # board_c5.h, board_c6.h, board_s3_guition.h
├── sdkconfig.defaults.c5
├── sdkconfig.defaults.c6
├── sdkconfig.defaults.s3_guition
└── partitions_8mb.csv
```

## CLI commands

Connect a terminal at 115200 baud after flashing (works on all boards).

```
bm> help                        # list all commands
bm> scan_ap                     # passive beacon/AP scan
bm> scan_raw                    # raw promiscuous capture
bm> sweep                       # passive sweep all 2.4+5 GHz channels (200ms dwell)
bm> sweep -d 100                # 100ms dwell
bm> sweep -2                    # 2.4 GHz only
bm> deauth -c 6 -n 50           # broadcast deauth flood ch 6, 50 frames
bm> deauth -b aa:bb:cc:dd:ee:ff -c 6 -n 20   # targeted deauth by BSSID
bm> beacon -s "FreeWiFi" -c 6  # beacon spam fixed SSID
bm> beacon -c 6                 # random SSID beacon spam
bm> eapol -c 6                  # EAPOL bad-message flood
bm> portal -s "Airport WiFi"   # evil captive portal (download PCAP via /pcap)
bm> pcap_start capture.pcap     # start radiotap PCAP frame capture to SD/SPIFFS
bm> pcap_status                 # show storage mount, recording state & frame count
bm> pcap_stop                   # stop PCAP recording and flush file
bm> ble_scan                    # passive BLE scan
bm> sour_apple                  # Apple proximity popup attack
bm> swiftpair                   # Microsoft Swift Pair spam
bm> ble_spam                    # all BLE vectors (Apple, MS, Samsung, Google, Apple Juice)
bm> stats                       # packet counters
bm> channel 149                 # jump to 5 GHz ch 149 (C5)
bm> stop                        # stop all attacks
```

## FreeRTOS task layout

### S3 Guition (Phase 4)
```
app_main (core 1)
  ├── spectrum_task  core 1  priority 4   — display render + touch dispatch (~25 fps)
  ├── cli_task       core 1  priority 5   — UART REPL
  ├── health_task    core 1  priority 2   — heap/stats log every 30s
  └── (attack tasks pinned to core 0, spawned on demand)
      wifi_attack    core 0  priority 20
      ble_attack     core 0  priority 19
```

### C5 / C6 (headless)
```
app_main (core 1)
  ├── cli_task       core 1  priority 5
  ├── health_task    core 1  priority 2
  └── (attack tasks on demand, core 0)
```

## Frame hook API

`wifi_attack_set_frame_hook(cb)` registers a `(uint8_t channel, int8_t rssi)` callback
invoked from the WiFi RX task on every parsed beacon. The spectrum visualizer uses this
to update per-channel RSSI state without a separate promiscuous registration.

```c
void my_hook(uint8_t channel, int8_t rssi) { /* fast, ISR-safe */ }
wifi_attack_set_frame_hook(my_hook);
```

## Building

Requires ESP-IDF 6.0.1+. On Windows:

```powershell
# Activate IDF (already installed at C:\esp\v6.0.1)
C:\esp\v6.0.1\export.ps1

# S3 Guition (Phase 4 — display + spectrum)
idf.py --sdkconfig-defaults sdkconfig.defaults.s3_guition set-target esp32s3 build
idf.py -p COM3 flash monitor

# C5 (5 GHz dual-band)
idf.py --sdkconfig-defaults sdkconfig.defaults.c5 set-target esp32c5 build
idf.py -p COM3 flash monitor

# C6 (Thread/Matter/Zigbee)
idf.py --sdkconfig-defaults sdkconfig.defaults.c6 set-target esp32c6 build
idf.py -p COM3 flash monitor
```

## Roadmap

- [x] Phase 2: WiFi attack core (deauth, beacon, EAPOL, evil portal), C5 5GHz
- [x] Phase 2: channel sweep CLI command with configurable dwell + band filter
- [x] Phase 2: PCAP capture with radiotap headers to SD card (SPIFFS fallback) + HTTP download
- [x] **Phase 4: Guition JC3248W535EN display + RSSI spectrum visualizer**
- [ ] Phase 3: Thread neighbor scan (`esp_openthread` / `otLinkActiveScan`)
- [ ] Phase 3: Matter commissioning sniffer (mDNS + PAKE intercept)
- [ ] Phase 3: Zigbee passive channel scan
- [ ] Phase 3: BLE 5.0 extended adv burst loops

## Related projects

- [jc3248-display-driver](https://github.com/GlomarGadaffi/jc3248-display-driver) — JC3248W535EN bring-up + display driver source
- [deauth-detector](https://github.com/GlomarGadaffi/deauth-detector) — 802.11 attack detection sensor (complement: detects what this generates)
- [wigle-wardriver](https://github.com/GlomarGadaffi/wigle-wardriver) — WiFi/BLE wardriver

## License

Apache 2.0
