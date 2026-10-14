idf_component_register(
    SRCS "spectrum.c"
    INCLUDE_DIRS "."
    REQUIRES display wifi_attack ble_attack freertos esp_timer pcap
)
