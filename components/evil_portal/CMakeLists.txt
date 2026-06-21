idf_component_register(
    SRCS "evil_portal.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_server lwip esp_wifi nvs_flash
)
