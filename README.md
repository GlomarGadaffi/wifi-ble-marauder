# binary-mist

Pure ESP-IDF 6.0.1 WiFi/BLE security research tool. No Arduino. No wrappers.

Successor to [ESP32Marauder](https://github.com/justcallmekoko/ESP32Marauder) — rewritten from the ground up in C against the native IDF stack, with new attack vectors that Arduino cannot touch.

## What's new vs. Marauder

| Capability | Marauder (Arduino) | binary-mist (IDF 6.0.1) |
|---|---|---|
| 5 GHz deauth + beacon spam | ✗ | ✓ ESP32-C5 |
| WiFi 6 (802.11ax) frame crafting | ✗ | ✓ C5/C6 |
| Thread neighbor topology dump | ✗ | ✓ C6 (Thread 1.3 certified) |
| Matter commissioning intercept | ✗ | ✓ C6 |
| Zigbee network enumeration | ✗ | ✓ C6 |
| BLE 5.0 extended adv (254-byte) | ✗ (31-byte limit) | ✓ C5/C6 |
| 802.15.4 + WiFi collision DoS | ✗ | ✓ C5/C6 |
| Multi-core task isolation | ✗ (single Arduino loop) | ✓ attack core 0, CLI core 1 |
| µs-precision EAPOL timing | ✗ | ✓ esp_timer |
| Multi-radio interleaved PCAP | ✗ | ✓ WiFi + BLE + 802.15.4 |

## Hardware targets

### Phase 2 — C5 (5 GHz attacks)
**[Espressif ESP32-C5-DevKitC-1-N8R4](https://www.espressif.com/en/products/devkits)**
- ESP32-C5, 240 MHz RISC-V
- Dual-band 2.4 + 5 GHz WiFi 6 (802.11ax)
- BLE 5.0, 8 MB Flash, 4 MB PSRAM
- Native USB-CDC — no FTDI

```bash
idf.py --sdkconfig-defaults sdkconfig.defaults.c5 set-target esp32c5 build flash monitor
```

### Phase 3 — C6 (Thread / Matter / Zigbee)
**[Seeed XIAO ESP32-C6](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/)**
- ESP32-C6, Thread 1.3 / Zigbee 3.x certified
- WiFi 6, BLE 5.3, 4 MB Flash, USB-C

```bash
idf.py --sdkconfig-defaults sdkconfig.defaults.c6 set-target esp32c6 build flash monitor
```

### Phase 4 — S3 + Guition display
**[Guition JC3248W535EN](https://github.com/GlomarGadaffi/stemmons-draftsman)**
- ESP32-S3, AXS15231B QSPI 320×480, FT5336 touch
- Display driver from [stemmons-draftsman](https://github.com/GlomarGadaffi/stemmons-draftsman)

## Project structure

```
binary-mist/
├── main/main.c                    # app_main — task spawning
├── components/
│   ├── wifi_attack/               # 802.11 frame inject/sniff (native esp_wifi)
│   ├── ble_attack/                # NimBLE attacks (esp_nimble direct)
│   ├── cli/                       # esp_console REPL
│   ├── pcap/                      # SD-backed PCAP writer with radiotap
│   ├── evil_portal/               # captive portal (esp_http_server + DNS)
│   ├── gps/                       # NMEA UART parser
│   └── board/                     # board_c5.h, board_c6.h, board_s3_guition.h
├── sdkconfig.defaults.c5
├── sdkconfig.defaults.c6
└── partitions_8mb.csv
```

## CLI commands

Connect a terminal at 115200 baud after flashing.

```
bm> help                  # list all commands
bm> scan_ap               # passive beacon/AP scan
bm> scan_raw              # raw promiscuous capture
bm> deauth -c 6 -n 50     # deauth flood ch 6, 50 frames
bm> beacon -s "FreeWiFi" -c 6   # beacon spam
bm> beacon -c 6           # random SSID beacon spam
bm> eapol -c 6            # EAPOL bad-message flood
bm> portal -s "Airport WiFi"    # evil captive portal
bm> ble_scan              # passive BLE scan
bm> sour_apple            # Apple proximity popup attack
bm> swiftpair             # Microsoft Swift Pair spam
bm> ble_spam              # all BLE vectors at once
bm> stats                 # packet counters
bm> channel 149           # jump to 5 GHz ch 149 (C5)
bm> stop                  # stop all attacks
```

## FreeRTOS task layout

```
app_main (core 1)
  ├── cli_task       core 1  priority 5   — interactive REPL
  ├── health_task    core 1  priority 2   — heap/stats log every 30s
  └── (attack tasks pinned to core 0, spawned on demand from CLI)
      wifi_attack    core 0  priority 20
      ble_attack     core 0  priority 19
```

## Building

Requires ESP-IDF 6.0.1+.

```bash
# Install IDF 6.0.1
git clone --recursive https://github.com/espressif/esp-idf.git --branch v6.0.1
cd esp-idf && ./install.sh && . ./export.sh

# Clone and build
git clone https://github.com/GlomarGadaffi/binary-mist
cd binary-mist

# C5 (5 GHz)
idf.py --sdkconfig-defaults sdkconfig.defaults.c5 set-target esp32c5 build
idf.py -p /dev/ttyUSB0 flash monitor

# C6 (Thread/Matter)
idf.py --sdkconfig-defaults sdkconfig.defaults.c6 set-target esp32c6 build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Roadmap

- [x] Phase 2: WiFi attack core (deauth, beacon, EAPOL, evil portal), C5 5GHz
- [ ] Phase 2: 5GHz automated channel sweep + PCAP with radiotap
- [ ] Phase 3: Thread neighbor scan (`esp_openthread` / `otLinkActiveScan`)
- [ ] Phase 3: Matter commissioning sniffer (mDNS + PAKE intercept)
- [ ] Phase 3: Zigbee passive channel scan
- [ ] Phase 3: BLE 5.0 extended adv burst loops
- [ ] Phase 4: Guition display UI from [stemmons-draftsman](https://github.com/GlomarGadaffi/stemmons-draftsman)
- [ ] Phase 4: Real-time RSSI spectrum visualizer on 320×480 touch display

## Related projects

- [stemmons-draftsman](https://github.com/GlomarGadaffi/stemmons-draftsman) — Phase 4 display driver (ESP32-S3 + Guition JC3248W535EN)
- [mastic-scout](https://github.com/GlomarGadaffi/mastic-scout) — 802.11 attack detection sensor (complement: detects what this generates)
- [ashburn-drifter](https://github.com/GlomarGadaffi/ashburn-drifter) — WiFi/BLE wardriver

## License

Apache 2.0
