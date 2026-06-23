/*
 * SPDX-FileCopyrightText: 2026 GlomarGadaffi
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ui.h"
#include "font8x8.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define BAND_ROWS 40   /* flush in 40-row bands (480 % 40 == 0) */

/* The framebuffer lives in PSRAM; DMAing straight from it to the panel can
 * underflow the SPI TX FIFO when another bus master (e.g. SDMMC) contends for
 * PSRAM bandwidth, wedging the LCD bus. So we copy each band into an internal-RAM
 * bounce buffer and DMA from there. Two buffers are used ping-pong so the CPU can
 * copy the next band while the current band's DMA is still in flight; s_done (a
 * counting semaphore, one give per completed transfer) bounds us to 2 in flight.
 * (Bands stay full-width and top-to-bottom, so the QSPI driver's RAMWR/RAMWRC
 * continuation scheme is preserved — transfers execute in FIFO queue order.) */
static uint16_t *s_bounce[2];         /* two bands, internal DMA-capable RAM */
static SemaphoreHandle_t s_done;      /* counting sem, given by ui_color_trans_done */

bool ui_color_trans_done(esp_lcd_panel_io_handle_t io,
                         esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    (void)io; (void)edata; (void)user_ctx;
    BaseType_t hp = pdFALSE;
    if (s_done) xSemaphoreGiveFromISR(s_done, &hp);
    return hp == pdTRUE;
}

void ui_init(ui_t *ui)
{
    (void)ui;
    if (!s_done) s_done = xSemaphoreCreateCounting(2, 0);
    for (int i = 0; i < 2; i++)
        if (!s_bounce[i])
            s_bounce[i] = heap_caps_malloc(LCD_H_RES * BAND_ROWS * sizeof(uint16_t),
                                           MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    /* On this board, DMAing the framebuffer straight from PSRAM underflows the SPI
     * FIFO and wedges the panel — so a failed bounce/sem alloc is a fatal
     * misconfiguration, not a soft fallback. Abort loudly rather than run degraded. */
    if (!s_done || !s_bounce[0] || !s_bounce[1])
        ESP_LOGE("ui", "bounce/sem alloc failed (need ~50KB internal DMA RAM)");
    ESP_ERROR_CHECK((s_done && s_bounce[0] && s_bounce[1]) ? ESP_OK : ESP_ERR_NO_MEM);
}

uint16_t ui_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t v = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((v >> 8) | (v << 8));   /* AXS15231B expects big-endian RGB565 */
}

void ui_fill(ui_t *ui, uint16_t color)
{
    for (int i = 0; i < LCD_H_RES * LCD_V_RES; i++) ui->fb[i] = color;
}

void ui_rect(ui_t *ui, int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_H_RES) w = LCD_H_RES - x;
    if (y + h > LCD_V_RES) h = LCD_V_RES - y;
    for (int yy = y; yy < y + h; yy++) {
        uint16_t *row = &ui->fb[yy * LCD_H_RES];
        for (int xx = x; xx < x + w; xx++) row[xx] = color;
    }
}

static inline void ui_px(ui_t *ui, int x, int y, uint16_t color)
{
    if ((unsigned)x < (unsigned)LCD_H_RES && (unsigned)y < (unsigned)LCD_V_RES)
        ui->fb[y * LCD_H_RES + x] = color;
}

void ui_circle(ui_t *ui, int cx, int cy, int r, uint16_t color)
{
    if (r < 0) return;
    int x = r, y = 0, err = 1 - r;   /* midpoint circle, 8-way symmetry */
    while (x >= y) {
        ui_px(ui, cx + x, cy + y, color); ui_px(ui, cx - x, cy + y, color);
        ui_px(ui, cx + x, cy - y, color); ui_px(ui, cx - x, cy - y, color);
        ui_px(ui, cx + y, cy + x, color); ui_px(ui, cx - y, cy + x, color);
        ui_px(ui, cx + y, cy - x, color); ui_px(ui, cx - y, cy - x, color);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void ui_text(ui_t *ui, int x, int y, const char *s, int scale, uint16_t color)
{
    if (scale < 1) scale = 1;
    int cx = x;
    for (const char *p = s; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch >= 128) ch = '?';
        const unsigned char *g = font8x8_basic[ch];
        for (int row = 0; row < 8; row++) {
            unsigned char bits = g[row];
            for (int col = 0; col < 8; col++) {
                if (bits & (1u << col))
                    ui_rect(ui, cx + col * scale, y + row * scale, scale, scale, color);
            }
        }
        cx += 8 * scale;
    }
}

int ui_text_w(const char *s, int scale)
{
    if (scale < 1) scale = 1;
    return (int)strlen(s) * 8 * scale;
}

void ui_flush(ui_t *ui)
{
    /* Full-screen, top-to-bottom in full-width bands: matches the QSPI driver's
     * RAMWR(top band) + RAMWRC(continuation) scheme. Double-buffered: band N+1's
     * memcpy overlaps band N's DMA. */
    if (!s_bounce[0] || !s_bounce[1] || !s_done) return;  /* unreachable: ui_init aborts on alloc failure */
    int inflight = 0, band = 0;
    for (int y = 0; y < LCD_V_RES; y += BAND_ROWS, band++) {
        /* Free a buffer before reusing it. Transfers complete in FIFO order, so
         * taking one give means the oldest (the buffer we're about to reuse) is done. */
        if (inflight == 2) { xSemaphoreTake(s_done, portMAX_DELAY); inflight--; }
        uint16_t *buf = s_bounce[band & 1];
        memcpy(buf, &ui->fb[y * LCD_H_RES], LCD_H_RES * BAND_ROWS * sizeof(uint16_t));
        /* Only wait for completion if the color transfer was actually queued:
         * draw_bitmap propagates errors and may return before issuing tx_color
         * (e.g. CASET failed), in which case no on_color_trans_done arrives. */
        if (esp_lcd_panel_draw_bitmap(ui->panel, 0, y, LCD_H_RES, y + BAND_ROWS, buf) == ESP_OK)
            inflight++;
    }
    while (inflight > 0) { xSemaphoreTake(s_done, portMAX_DELAY); inflight--; }
}

void ui_back_bar(ui_t *ui)
{
    ui_rect(ui, 0, 0, LCD_H_RES, UI_BACK_H, ui_rgb(60, 60, 60));
    ui_text(ui, 12, (UI_BACK_H - 8 * 2) / 2, "< BACK", 2, ui_rgb(255, 255, 255));
}

bool ui_in_back(uint16_t x, uint16_t y)
{
    (void)x;
    return y < UI_BACK_H;
}

bool board_touch(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y)
{
    if (esp_lcd_touch_read_data(tp) != ESP_OK) return false;
    esp_lcd_touch_point_data_t pt[1];
    uint8_t n = 0;
    if (esp_lcd_touch_get_data(tp, pt, &n, 1) == ESP_OK && n > 0) {
        *x = pt[0].x;
        *y = pt[0].y;
        return true;
    }
    return false;
}
