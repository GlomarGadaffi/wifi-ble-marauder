idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        wifi_attack ble_attack cli pcap gps evil_portal
        display spectrum esp_lcd_axs15231b board
        nvs_flash esp_timer freertos
)

target_compile_definitions(__idf_main PRIVATE BOARD_S3_GUITION=1)
