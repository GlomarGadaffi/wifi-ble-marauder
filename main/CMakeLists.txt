idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        wifi_attack ble_attack cli pcap gps evil_portal
        display spectrum esp_lcd_axs15231b
        nvs_flash esp_timer freertos
)
