idf_component_register(
    SRCS "display.c" "ui.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_lcd_axs15231b esp_lcd esp_lcd_touch
        driver esp_driver_ledc esp_driver_gpio esp_driver_spi esp_driver_i2c
        esp_timer freertos
)
