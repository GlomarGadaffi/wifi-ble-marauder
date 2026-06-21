idf_component_register(
    SRCS "cli.c"
    INCLUDE_DIRS "."
    REQUIRES console wifi_attack ble_attack pcap nvs_flash
)
