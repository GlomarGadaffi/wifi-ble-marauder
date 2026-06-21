#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double   lat;
    double   lon;
    double   alt;
    float    speed_knots;
    uint8_t  satellites;
    bool     valid;
    uint32_t timestamp_ms;
} gps_fix_t;

esp_err_t  gps_init(int tx_pin, int rx_pin, int uart_num);
esp_err_t  gps_deinit(void);
bool       gps_get_fix(gps_fix_t *out);
bool       gps_has_fix(void);

#ifdef __cplusplus
}
#endif
