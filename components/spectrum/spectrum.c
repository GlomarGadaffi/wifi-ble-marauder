/*
 * RSSI spectrum visualizer — renders live 2.4 GHz channel activity on the
 * Guition JC3248W535EN display and dispatches touch-triggered WiFi attacks.
 *
 * spectrum_update() is called from the wifi_attack frame hook (WiFi RX task).
 * spectrum_task() is the FreeRTOS display loop running on core 1.
 */
#include "spectrum.h"
#include "display.h"
#include "board.h"
#include "ui.h"
#include "wifi_attack.h"
#include "ble_attack.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "spectrum";

/* ── Layout constants (320×480 portrait) ────────────────────────────── */
#define HDR_Y        0
#define HDR_H       42
#define CHART_Y     42
#define CHART_H    200        /* usable bar height in pixels */
#define LIST_Y     242
#define LIST_H     158
#define BTN_Y      400
#define BTN_H       80

#define BAR_W       22
#define BAR_GAP      2
#define BAR_STRIDE  (BAR_W + BAR_GAP)   /* 24 px per channel */
#define BAR_MARGIN   4                   /* left margin */
/* 13 × 24 + 4 = 316 ≤ 320 ✓ */

#define CHART_BOTTOM  (CHART_Y + CHART_H)

/* ── AP list ring ────────────────────────────────────────────────────── */
#define AP_LIST_MAX  6

typedef struct {
    char    ssid[33];
    uint8_t channel;
    int8_t  rssi;
} ap_entry_t;

/* ── Per-channel state ───────────────────────────────────────────────── */
typedef struct {
    int8_t   peak_rssi;     /* strongest RSSI seen; decays toward -100 */
    uint32_t last_ms;       /* esp_timer_get_time()/1000 of last update */
} ch_state_t;

static ch_state_t s_ch[SPECTRUM_CH24_COUNT];   /* index 0 → channel 1 */
static ap_entry_t s_aps[AP_LIST_MAX];
static int        s_ap_head = 0;              /* ring insert pointer */
static uint8_t    s_active_ch = 6;           /* touch-selected channel */
static bool       s_needs_redraw = true;      /* full redraw on first frame */

/* ── Colour helpers ──────────────────────────────────────────────────── */
static uint16_t bar_color(int8_t rssi)
{
    if (rssi >= -60) return ui_rgb(220,  50,  50);   /* red   — very strong */
    if (rssi >= -75) return ui_rgb(230, 180,  30);   /* yellow — strong     */
    if (rssi >= -90) return ui_rgb( 50, 200,  80);   /* green  — moderate   */
    return               ui_rgb( 30,  90, 100);      /* teal   — weak       */
}

/* ── Public: update from WiFi RX task ───────────────────────────────── */
void spectrum_init(void)
{
    for (int i = 0; i < SPECTRUM_CH24_COUNT; i++) {
        s_ch[i].peak_rssi = -100;
        s_ch[i].last_ms   = 0;
    }
}

void spectrum_update(uint8_t channel, int8_t rssi)
{
    if (channel < 1 || channel > SPECTRUM_CH24_COUNT) return;
    int idx = channel - 1;
    if (rssi > s_ch[idx].peak_rssi) {
        s_ch[idx].peak_rssi = rssi;
    }
    s_ch[idx].last_ms = (uint32_t)(esp_timer_get_time() / 1000);
}

/* Called after a beacon is parsed to add to AP list ring (not ISR, but
 * called from the wifi RX task — keep it fast). */
static void ap_list_push(const char *ssid, uint8_t channel, int8_t rssi)
{
    ap_entry_t *e = &s_aps[s_ap_head % AP_LIST_MAX];
    strncpy(e->ssid, ssid, 32);
    e->ssid[32] = '\0';
    e->channel  = channel;
    e->rssi     = rssi;
    s_ap_head++;
}

/* ── Rendering ───────────────────────────────────────────────────────── */

static void render_header(ui_t *ui, const char *mode)
{
    ui_rect(ui, 0, HDR_Y, LCD_H_RES, HDR_H, ui_rgb(18, 22, 36));
    ui_text(ui, 6, 8, "binary-mist", 2, ui_rgb(80, 200, 255));

    char chbuf[10];
    snprintf(chbuf, sizeof(chbuf), "CH:%d", s_active_ch);
    ui_text(ui, 192, 8, chbuf, 2, ui_rgb(220, 220, 80));

    ui_text(ui, 6, 26, mode, 1, ui_rgb(120, 130, 150));
}

static void render_chart(ui_t *ui, uint32_t now_ms)
{
    /* chart background */
    ui_rect(ui, 0, CHART_Y, LCD_H_RES, CHART_H, ui_rgb(12, 14, 22));

    /* faint horizontal gridlines at -60, -75, -90 dBm */
    static const int8_t grid_rssi[] = { -60, -75, -90 };
    for (int g = 0; g < 3; g++) {
        int h = (grid_rssi[g] + 100) * (CHART_H - 10) / 70;
        int gy = CHART_BOTTOM - h;
        ui_rect(ui, 0, gy, LCD_H_RES, 1, ui_rgb(35, 40, 55));
    }

    for (int i = 0; i < SPECTRUM_CH24_COUNT; i++) {
        /* decay: after SPECTRUM_DECAY_MS with no signal, ramp RSSI back */
        ch_state_t *ch = &s_ch[i];
        uint32_t age = now_ms - ch->last_ms;
        int8_t display_rssi = ch->peak_rssi;
        if (ch->last_ms > 0 && age > SPECTRUM_DECAY_MS) {
            /* full decay after 2× period */
            uint32_t extra = age - SPECTRUM_DECAY_MS;
            int drop = (int)(extra * 70 / SPECTRUM_DECAY_MS);
            display_rssi = (int8_t)(ch->peak_rssi + drop);
            if (display_rssi > -30) display_rssi = -30;
            /* clamp so we don't write back to the source */
        }
        if (display_rssi < -100) display_rssi = -100;

        int bar_h = (display_rssi + 100) * (CHART_H - 10) / 70;
        if (bar_h < 2) bar_h = 2;   /* always visible minimum nub */

        int bx = BAR_MARGIN + i * BAR_STRIDE;
        int by = CHART_BOTTOM - bar_h;
        uint8_t ch_num = (uint8_t)(i + 1);

        /* highlight selected channel */
        uint16_t col = (ch_num == s_active_ch)
                       ? ui_rgb(255, 255, 80)   /* bright yellow selected */
                       : bar_color(display_rssi);

        ui_rect(ui, bx, by, BAR_W, bar_h, col);

        /* channel number label below chart */
        char lbl[4];
        snprintf(lbl, sizeof(lbl), "%d", ch_num);
        int lx = bx + BAR_W / 2 - (ch_num > 9 ? 6 : 3);
        ui_text(ui, lx, CHART_BOTTOM + 2, lbl, 1, ui_rgb(160, 165, 180));
    }

    /* dBm scale on right */
    ui_text(ui, LCD_H_RES - 28, CHART_Y + 2,      "-30", 1, ui_rgb(90, 90, 110));
    ui_text(ui, LCD_H_RES - 28, CHART_BOTTOM - 10, "-100", 1, ui_rgb(90, 90, 110));
}

static void render_ap_list(ui_t *ui)
{
    ui_rect(ui, 0, LIST_Y, LCD_H_RES, LIST_H, ui_rgb(14, 17, 28));
    ui_text(ui, 6, LIST_Y + 4, "Recent APs:", 1, ui_rgb(100, 110, 140));

    int count = (s_ap_head < AP_LIST_MAX) ? s_ap_head : AP_LIST_MAX;
    for (int i = 0; i < count; i++) {
        int slot = (s_ap_head - 1 - i + AP_LIST_MAX * 2) % AP_LIST_MAX;
        ap_entry_t *e = &s_aps[slot];
        char line[48];
        snprintf(line, sizeof(line), "ch%2d %4ddBm %.20s",
                 e->channel, e->rssi, e->ssid);
        ui_text(ui, 6, LIST_Y + 16 + i * 22, line, 1, ui_rgb(180, 185, 200));
    }
}

static void render_action_bar(ui_t *ui)
{
    static const char *labels[] = { "DEAUTH", "BEACON", "SWEEP", "STOP" };
    static const uint16_t cols[] = {
        0, /* red   computed below */
        0, /* blue  */
        0, /* green */
        0, /* gray  */
    };
    (void)cols;
    const uint16_t bg_cols[4] = {
        ui_rgb(140, 30, 30),
        ui_rgb(30, 60, 140),
        ui_rgb(30, 100, 50),
        ui_rgb(60, 60, 70),
    };
    int btn_w = LCD_H_RES / 4;   /* 80px each */
    for (int i = 0; i < 4; i++) {
        int bx = i * btn_w;
        ui_rect(ui, bx,     BTN_Y,     btn_w - 2, BTN_H, bg_cols[i]);
        ui_rect(ui, bx,     BTN_Y,     btn_w - 2, 2, ui_rgb(80,90,110));  /* top border */
        /* center text */
        int tx = bx + btn_w / 2 - (int)(strlen(labels[i]) * 6) / 2;
        ui_text(ui, tx, BTN_Y + BTN_H / 2 - 4, labels[i], 1, ui_rgb(230, 235, 240));
    }
}

/* ── Touch dispatch ──────────────────────────────────────────────────── */

static void handle_touch(uint16_t tx, uint16_t ty)
{
    if (ty >= BTN_Y) {
        /* Action bar */
        int btn = tx / (LCD_H_RES / 4);
        static const uint8_t bcast[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
        switch (btn) {
            case 0:   /* DEAUTH */
                wifi_attack_init();
                wifi_attack_send_deauth(bcast, bcast, s_active_ch, 20);
                ESP_LOGI(TAG, "deauth x20 ch%d", s_active_ch);
                break;
            case 1:   /* BEACON */
                wifi_attack_init();
                wifi_attack_beacon_rand_ssid(s_active_ch);
                ESP_LOGI(TAG, "beacon spam ch%d", s_active_ch);
                break;
            case 2: { /* SWEEP */
                wifi_attack_init();
                wifi_attack_start_promiscuous(wifi_attack_beacon_sniffer_cb);
                extern const uint8_t dual_band_channels[];
                extern const uint8_t dual_band_channel_count;
                for (uint8_t i = 0; i < dual_band_channel_count; i++) {
                    uint8_t ch = dual_band_channels[i];
                    if (ch > 13) break;   /* 2.4 GHz only from display */
                    wifi_attack_set_channel(ch);
                    vTaskDelay(pdMS_TO_TICKS(200));
                }
                wifi_attack_stop_promiscuous();
                ESP_LOGI(TAG, "sweep done");
                break;
            }
            case 3:   /* STOP */
                wifi_attack_stop_promiscuous();
                wifi_attack_deinit();
                ble_attack_stop_scan();
                ESP_LOGI(TAG, "all stopped");
                break;
        }
        return;
    }

    if (ty >= CHART_Y && ty < CHART_BOTTOM) {
        /* Channel bar tap */
        if (tx < BAR_MARGIN) return;
        int idx = (tx - BAR_MARGIN) / BAR_STRIDE;
        if (idx >= 0 && idx < SPECTRUM_CH24_COUNT) {
            s_active_ch = (uint8_t)(idx + 1);
            wifi_attack_set_channel(s_active_ch);
            ESP_LOGI(TAG, "active channel -> %d", s_active_ch);
        }
    }
}

/* ── Task ────────────────────────────────────────────────────────────── */

void spectrum_task(void *arg)
{
    bm_display_t *disp = (bm_display_t *)arg;
    ui_t *ui = &disp->ui;

    spectrum_init();
    wifi_attack_set_frame_hook(spectrum_update);

    /* Start passive promiscuous immediately so bars light up without
     * requiring the user to type a CLI command first. */
    wifi_attack_init();
    wifi_attack_start_promiscuous(wifi_attack_beacon_sniffer_cb);

    ESP_LOGI(TAG, "spectrum task running on core %d", (int)xPortGetCoreID());

    bool prev_touch = false;

    while (1) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

        /* Touch */
        uint16_t tx = 0, ty = 0;
        bool touched = bm_display_touch_read(disp, &tx, &ty);
        if (touched && !prev_touch) {
            handle_touch(tx, ty);
        }
        prev_touch = touched;

        /* Render */
        render_header(ui, "2.4 GHz RSSI spectrum");
        render_chart(ui, now_ms);
        render_ap_list(ui);
        render_action_bar(ui);
        ui_flush(ui);

        vTaskDelay(pdMS_TO_TICKS(40));   /* ~25 fps */
    }
}
