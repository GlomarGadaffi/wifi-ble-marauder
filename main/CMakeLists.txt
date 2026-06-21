idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES wifi_attack ble_attack cli pcap gps evil_portal nvs_flash esp_timer freertos
)
