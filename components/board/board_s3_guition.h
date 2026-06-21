#pragma once

/* Guition JC3248W535EN — ESP32-S3, AXS15231B QSPI 320x480, FT5336 touch
 * Source: stemmons-draftsman/main/board.h
 * QSPI max 40 MHz — 80 MHz breaks I2C touch (GPIO 8/4 crosstalk)
 * Call esp_lcd_panel_disp_on_off(panel, true) after init — DISPON missing from mfr table
 */

#define BOARD_NAME          "Guition-JC3248W535EN"

/* LCD QSPI */
#define BOARD_LCD_CS        45
#define BOARD_LCD_CLK       47
#define BOARD_LCD_D0        21
#define BOARD_LCD_D1        48
#define BOARD_LCD_D2        40
#define BOARD_LCD_D3        39
#define BOARD_LCD_RST       -1

/* Backlight (LEDC PWM) */
#define BOARD_LCD_BL        1
#define BOARD_LCD_BL_FREQ   5000
#define BOARD_LCD_BL_RES    10

/* Touch FT5336 (I2C) */
#define BOARD_TOUCH_SCL     8
#define BOARD_TOUCH_SDA     4
#define BOARD_TOUCH_ADDR    0x3B
#define BOARD_TOUCH_RST     -1
#define BOARD_TOUCH_INT     -1
#define BOARD_LCD_QSPI_HZ   (40 * 1000 * 1000)   /* 40 MHz max */

/* LCD resolution */
#define BOARD_LCD_W         320
#define BOARD_LCD_H         480

/* SD card (1-bit SDIO) */
#define BOARD_SD_CLK        12
#define BOARD_SD_CMD        11
#define BOARD_SD_D0         13

/* Audio I2S */
#define BOARD_I2S_BCLK      42
#define BOARD_I2S_WS        2
#define BOARD_I2S_DOUT      41

/* Battery ADC (2:1 divider, 3000 mV = 0%, 4200 mV = 100%) */
#define BOARD_BATT_ADC      5

#define BOARD_HAS_5GHZ      0
#define BOARD_HAS_PSRAM     1   /* 8 MB OPI PSRAM */
#define BOARD_HAS_SD        1
#define BOARD_HAS_DISPLAY   1
#define BOARD_HAS_TOUCH     1
#define BOARD_HAS_AUDIO     1
#define BOARD_HAS_BATTERY   1
