#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Scan / attack mode IDs ─────────────────────────────────────────── */
#define WIFI_MODE_OFF                   0
#define WIFI_MODE_PROBE_SNIFF           1
#define WIFI_MODE_AP_SNIFF              2
#define WIFI_MODE_PWN_SNIFF             3
#define WIFI_MODE_EAPOL_SNIFF           4
#define WIFI_MODE_DEAUTH_SNIFF          5
#define WIFI_MODE_BEACON_SPAM           6
#define WIFI_MODE_DEAUTH_ALL            7
#define WIFI_MODE_DEAUTH_TARGETED       8
#define WIFI_MODE_AUTH_FLOOD            9
#define WIFI_MODE_EAPOL_BAD_MSG         10
#define WIFI_MODE_BEACON_RICKROLL       11
#define WIFI_MODE_RAW_CAPTURE           12
#define WIFI_MODE_EVIL_PORTAL           13
#define WIFI_MODE_WAR_DRIVE             14
#define WIFI_MODE_SAE_COMMIT            15
#define WIFI_MODE_CSA                   16

/* ── Dual-band channel list (C5 only) ───────────────────────────────── */
#define CHAN_5GHZ_START                 36
extern const uint8_t dual_band_channels[];
extern const uint8_t dual_band_channel_count;

/* ── Callback types ─────────────────────────────────────────────────── */
typedef void (*wifi_frame_cb_t)(void *buf, wifi_promiscuous_pkt_type_t type);

/* Called from beacon sniffer on each parsed beacon: channel, rssi.
 * Register via wifi_attack_set_frame_hook(); replaces any previous hook.
 * Invoked from the Wi-Fi RX task — must be short and ISR-safe. */
typedef void (*wifi_attack_frame_hook_t)(uint8_t channel, int8_t rssi);
void wifi_attack_set_frame_hook(wifi_attack_frame_hook_t hook);

/* ── Result structures ──────────────────────────────────────────────── */
typedef struct {
    uint8_t  bssid[6];
    char     ssid[33];
    uint8_t  ssid_len;
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  security;
    uint8_t  beacon[2];
} bm_ap_t;

typedef struct {
    uint8_t  mac[6];
    uint8_t  bssid[6];
    int8_t   rssi;
    uint32_t last_seen_ms;
    char     ssid[33];
} bm_station_t;

/* ── Public API ─────────────────────────────────────────────────────── */
esp_err_t wifi_attack_init(void);
esp_err_t wifi_attack_deinit(void);

esp_err_t wifi_attack_start_promiscuous(wifi_frame_cb_t cb);
esp_err_t wifi_attack_stop_promiscuous(void);

esp_err_t wifi_attack_set_channel(uint8_t channel);
uint8_t   wifi_attack_get_channel(void);

esp_err_t wifi_attack_send_deauth(const uint8_t bssid[6], const uint8_t dst[6],
                                   uint8_t channel, uint8_t count);
esp_err_t wifi_attack_beacon_spam(const char *ssid, uint8_t channel, bool random_mac);
esp_err_t wifi_attack_beacon_rand_ssid(uint8_t channel);
esp_err_t wifi_attack_send_eapol_bad(const uint8_t bssid[6], uint8_t channel);
esp_err_t wifi_attack_probe_req(const char *ssid, uint8_t channel);

void wifi_attack_beacon_sniffer_cb(void *buf, wifi_promiscuous_pkt_type_t type);
void wifi_attack_ap_sniffer_cb(void *buf, wifi_promiscuous_pkt_type_t type);

uint32_t wifi_attack_packets_sent(void);
uint32_t wifi_attack_packets_rx(void);
void     wifi_attack_reset_stats(void);

esp_err_t wifi_attack_enable_5ghz(bool enable);
bool      wifi_attack_has_5ghz(void);

#ifdef __cplusplus
}
#endif
