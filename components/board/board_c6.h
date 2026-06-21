#pragma once

/* Seeed XIAO ESP32-C6 pin definitions
 * USB-C native, 4 MB Flash, no PSRAM
 * Thread 1.3 / Zigbee 3.x certified
 */

#define BOARD_NAME          "Seeed-XIAO-ESP32-C6"

/* UART0 — USB-CDC */
#define BOARD_UART_TX       16
#define BOARD_UART_RX       17

/* SD card — XIAO has no SD slot; add breakout if needed */
#define BOARD_SD_MOSI       4
#define BOARD_SD_MISO       5
#define BOARD_SD_CLK        6
#define BOARD_SD_CS         7

/* GPS UART */
#define BOARD_GPS_TX        20
#define BOARD_GPS_RX        21

/* Status LED (D10 = GPIO22 on XIAO-C6) */
#define BOARD_LED_GPIO      22

#define BOARD_HAS_5GHZ      0
#define BOARD_HAS_PSRAM     0
#define BOARD_HAS_SD        0
#define BOARD_HAS_GPS       0
#define BOARD_HAS_DISPLAY   0
#define BOARD_HAS_THREAD    1
#define BOARD_HAS_ZIGBEE    1
