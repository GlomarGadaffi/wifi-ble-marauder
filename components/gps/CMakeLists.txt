idf_component_register(
    SRCS "gps.c"
    INCLUDE_DIRS "."
    REQUIRES esp_timer
    PRIV_REQUIRES esp_driver_uart
)
