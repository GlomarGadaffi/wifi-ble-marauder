/*
 * SPDX-FileCopyrightText: 2026 GlomarGadaffi
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tiny software-framebuffer UI for the AXS15231B QSPI panel. No LVGL — just a
 * PSRAM framebuffer, a few draw primitives, and a banded flush that matches the
 * driver's RAMWR(y==0)/RAMWRC continuation contract. Colors are stored already
 * byte-swapped to the panel's big-endian RGB565 (use ui_rgb()).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "board.h"

typedef struct {
    uint16_t *fb;                  /*!< LCD_H_RES*LCD_V_RES, panel-endian RGB565 */
    esp_lcd_panel_handle_t panel;
} ui_t;

#define UI_BACK_H 48   /* height of the top "back" bar each demo draws */

/* Allocate the internal-RAM bounce buffer + flush sync. Call once after the
 * panel is created and before the first ui_flush(). */
void ui_init(ui_t *ui);

/* Panel color-trans-done callback — wire into the panel IO config so ui_flush
 * can wait for each band's DMA to finish before reusing the bounce buffer. */
bool ui_color_trans_done(esp_lcd_panel_io_handle_t io,
                         esp_lcd_panel_io_event_data_t *edata, void *user_ctx);

/* Pack r,g,b into the panel's big-endian RGB565. */
uint16_t ui_rgb(uint8_t r, uint8_t g, uint8_t b);

/* Framebuffer drawing (does NOT push to the panel; call ui_flush). */
void ui_fill(ui_t *ui, uint16_t color);
void ui_rect(ui_t *ui, int x, int y, int w, int h, uint16_t color);

/* Draw a 1px circle outline (clipped to the panel). */
void ui_circle(ui_t *ui, int cx, int cy, int r, uint16_t color);

/* Draw text with the built-in 8x8 font, transparent background (only the set
 * pixels are drawn, in `color`). `scale` enlarges each pixel (1 = 8px tall). */
void ui_text(ui_t *ui, int x, int y, const char *s, int scale, uint16_t color);
/* Pixel width a string would occupy at the given scale. */
int ui_text_w(const char *s, int scale);

/* Push the whole framebuffer to the panel (full-screen, top-to-bottom). */
void ui_flush(ui_t *ui);

/* Draw the standard top "back" bar; ui_in_back() tests a touch against it. */
void ui_back_bar(ui_t *ui);
bool ui_in_back(uint16_t x, uint16_t y);

/* Read a single touch point (panel coordinates). Returns true if pressed. */
bool board_touch(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y);
