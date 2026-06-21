idf_component_register(
    SRCS "ble_attack.c"
    INCLUDE_DIRS "."
    REQUIRES bt nvs_flash esp_timer
)
