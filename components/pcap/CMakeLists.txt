idf_component_register(
    SRCS "pcap.c"
    INCLUDE_DIRS "."
    REQUIRES fatfs sdmmc esp_timer spiffs vfs esp_driver_sdmmc
)
