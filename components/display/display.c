/*
 * Board-level display bring-up for the Guition JC3248W535EN.
 * Wraps QSPI panel init, PWM backlight, and I2C touch into bm_display_t.
 *
 * Key invariant: call esp_lcd_panel_disp_on_off(panel, true) after init —
 * the AXS15231B init table does NOT issue DISPON, so the panel stays black
 * without it. See stemmons-draftsman notes for details.
 */
#include "display.h"
#include "board.h"
#include "esp_lcd_axs15231b.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "bm_display";

void bm_display_backlight_set(int duty)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void backlight_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num       = LEDC_TIMER_1,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = {
        .gpio_num   = PIN_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_1,
        .intr_type  = LEDC_INTR_DISABLE,
        .duty       = BM_BL_FULL,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

static esp_lcd_panel_handle_t panel_init(void)
{
    const spi_bus_config_t buscfg = AXS15231B_PANEL_BUS_QSPI_CONFIG(
        PIN_SCK, PIN_D0, PIN_D1, PIN_D2, PIN_D3,
        LCD_H_RES * LCD_V_RES * sizeof(uint16_t));
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg =
        AXS15231B_PANEL_IO_QSPI_CONFIG(PIN_CS, ui_color_trans_done, NULL);
    io_cfg.pclk_hz = 40 * 1000 * 1000;   /* 40 MHz stable; 80 MHz couples into I2C */
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));

    axs15231b_vendor_config_t vendor = { .flags = { .use_qspi_interface = 1 } };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config  = &vendor,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(io, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    return panel;
}

static esp_lcd_touch_handle_t touch_init(void)
{
    i2c_master_bus_handle_t i2c_bus = NULL;
    const i2c_master_bus_config_t i2c_cfg = {
        .i2c_port              = I2C_NUM_0,
        .sda_io_num            = PIN_TOUCH_SDA,
        .scl_io_num            = PIN_TOUCH_SCL,
        .clk_source            = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt     = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &i2c_bus));

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg =
        ESP_LCD_TOUCH_IO_I2C_AXS15231B_CONFIG();
    tp_io_cfg.scl_speed_hz = TOUCH_I2C_HZ;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_cfg, &tp_io));

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max        = LCD_H_RES,
        .y_max        = LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
    };
    esp_lcd_touch_handle_t tp = NULL;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_axs15231b(tp_io, &tp_cfg, &tp));
    return tp;
}

esp_err_t bm_display_init(bm_display_t *disp)
{
    backlight_init();
    disp->panel = panel_init();

    uint16_t *fb = heap_caps_malloc(LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fb) {
        ESP_LOGE(TAG, "PSRAM framebuffer alloc failed");
        return ESP_ERR_NO_MEM;
    }
    disp->ui = (ui_t){ .fb = fb, .panel = disp->panel };
    ui_init(&disp->ui);

    disp->touch = touch_init();

    ESP_LOGI(TAG, "display ready — %dx%d  fb=%p", LCD_H_RES, LCD_V_RES, fb);
    return ESP_OK;
}

bool bm_display_touch_read(bm_display_t *disp, uint16_t *x, uint16_t *y)
{
    if (esp_lcd_touch_read_data(disp->touch) != ESP_OK) return false;
    esp_lcd_touch_point_data_t pt[1];
    uint8_t n = 0;
    if (esp_lcd_touch_get_data(disp->touch, pt, &n, 1) == ESP_OK && n > 0) {
        *x = pt[0].x;
        *y = pt[0].y;
        return true;
    }
    return false;
}
