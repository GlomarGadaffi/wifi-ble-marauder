#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t   pcap_mount_auto(void);
esp_err_t   pcap_init(const char *mount_point);
esp_err_t   pcap_open(const char *filename);
esp_err_t   pcap_write_frame(const uint8_t *data, uint16_t cap_len, uint16_t orig_len, int8_t rssi, uint8_t channel);
esp_err_t   pcap_write_packet(const void *payload, uint16_t len, int8_t rssi, uint8_t channel);
esp_err_t   pcap_flush(void);
esp_err_t   pcap_close(void);
esp_err_t   pcap_deinit(void);

bool        pcap_is_mounted(void);
bool        pcap_is_active(void);
bool        pcap_is_sd(void);
const char *pcap_get_mount_point(void);
const char *pcap_get_active_file(void);
uint32_t    pcap_frame_count(void);

#ifdef __cplusplus
}
#endif
