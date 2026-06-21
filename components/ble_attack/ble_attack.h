#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── BLE attack mode IDs ────────────────────────────────────────────── */
#define BLE_MODE_OFF                0
#define BLE_MODE_SCAN_ALL           1
#define BLE_MODE_SCAN_AIRTAG        2
#define BLE_MODE_SCAN_FLIPPER       3
#define BLE_MODE_ATTACK_SOUR_APPLE  4
#define BLE_MODE_ATTACK_SWIFTPAIR   5
#define BLE_MODE_ATTACK_SAMSUNG     6
#define BLE_MODE_ATTACK_GOOGLE      7
#define BLE_MODE_ATTACK_APPLE_JUICE 8
#define BLE_MODE_ATTACK_SPAM_ALL    9

/* ── BLE device record ──────────────────────────────────────────────── */
typedef struct {
    uint8_t  addr[6];
    char     name[32];
    int8_t   rssi;
    uint8_t  payload[62];
    uint8_t  payload_len;
    uint32_t last_seen_ms;
} bm_ble_device_t;

typedef void (*ble_device_cb_t)(const bm_ble_device_t *dev);

/* ── Public API ─────────────────────────────────────────────────────── */
esp_err_t ble_attack_init(void);
esp_err_t ble_attack_deinit(void);

esp_err_t ble_attack_start_scan(ble_device_cb_t cb);
esp_err_t ble_attack_stop_scan(void);

/* Spam attacks — each call sends one burst; loop from task */
esp_err_t ble_attack_sour_apple(void);
esp_err_t ble_attack_swiftpair(void);
esp_err_t ble_attack_samsung(void);
esp_err_t ble_attack_google(void);
esp_err_t ble_attack_apple_juice(void);

/* Extended advertising (BLE 5.0, C5/C6 only, up to 254-byte payload) */
esp_err_t ble_attack_ext_adv_start(const uint8_t *payload, uint8_t len);
esp_err_t ble_attack_ext_adv_stop(void);

uint32_t ble_attack_frames_sent(void);
void     ble_attack_reset_stats(void);

#ifdef __cplusplus
}
#endif
