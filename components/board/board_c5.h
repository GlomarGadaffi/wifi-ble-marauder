#pragma once

/* ESP32-C5-DevKitC-1-N8R4 pin definitions
 * USB-CDC native — no FTDI needed
 * 8 MB Flash, 4 MB PSRAM
 */

#define BOARD_NAME          "ESP32-C5-DevKitC-1"

/* UART0 — USB-CDC virtual COM */
#define BOARD_UART_TX       21
#define BOARD_UART_RX       20

/* SD card (SPI mode) — optional, wire your own */
#define BOARD_SD_MOSI       10
#define BOARD_SD_MISO       11
#define BOARD_SD_CLK        12
#define BOARD_SD_CS         13

/* GPS UART — optional, wire NMEA module to these */
#define BOARD_GPS_TX        6
#define BOARD_GPS_RX        7

/* Status LED (GPIO8 = onboard RGB on DevKitC-1) */
#define BOARD_LED_GPIO      8

/* Dual-band WiFi — no extra GPIO, handled by rf switch on-module */
#define BOARD_HAS_5GHZ      1
#define BOARD_HAS_PSRAM     1
#define BOARD_HAS_SD        0   /* set 1 if SD wired */
#define BOARD_HAS_GPS       0   /* set 1 if GPS wired */
#define BOARD_HAS_DISPLAY   0
