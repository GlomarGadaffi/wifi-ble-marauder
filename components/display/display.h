#pragma once

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_panel_ops.h"
#include "ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BM_BL_FULL  1023
#define BM_BL_DIM    180

typedef struct {
    esp_lcd_panel_handle_t panel;
    esp_lcd_touch_handle_t touch;
    ui_t                   ui;
} bm_display_t;

esp_err_t bm_display_init(bm_display_t *disp);
void      bm_display_backlight_set(int duty);
bool      bm_display_touch_read(bm_display_t *disp, uint16_t *x, uint16_t *y);

#ifdef __cplusplus
}
#endif
