#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t pcap_init(const char *mount_point);
esp_err_t pcap_open(const char *filename);
esp_err_t pcap_write_frame(const uint8_t *data, uint16_t len, int8_t rssi, uint8_t channel);
esp_err_t pcap_close(void);
esp_err_t pcap_deinit(void);

#ifdef __cplusplus
}
#endif
