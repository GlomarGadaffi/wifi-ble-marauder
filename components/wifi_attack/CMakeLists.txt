idf_component_register(
    SRCS "wifi_attack.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif nvs_flash mbedtls esp_timer freertos
)
