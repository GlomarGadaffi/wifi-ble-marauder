#include "pcap.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "pcap";

/* PCAP global header */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic_number;
    uint16_t version_major;
    uint16_t version_minor;
    int32_t  thiszone;
    uint32_t sigfigs;
    uint32_t snaplen;
    uint32_t network;   /* 105 = IEEE 802.11 radiotap */
} pcap_global_hdr_t;

typedef struct {
    uint32_t ts_sec;
    uint32_t ts_usec;
    uint32_t incl_len;
    uint32_t orig_len;
} pcap_rec_hdr_t;

/* Minimal radiotap header with channel + RSSI */
typedef struct {
    uint8_t  it_version;
    uint8_t  it_pad;
    uint16_t it_len;
    uint32_t it_present;  /* flags: TSFT(0), FLAGS(1), RATE(2), CHANNEL(3), DBM_ANTSIGNAL(5) */
    uint64_t tsft;
    uint8_t  flags;
    uint8_t  rate;        /* 0x02 = 1 Mbps */
    uint16_t channel_freq;
    uint16_t channel_flags;
    int8_t   dbm_antsignal;
    uint8_t  _pad;
} radiotap_hdr_t;
#pragma pack(pop)

static FILE *s_fp = NULL;
static bool  s_mounted = false;
static char  s_mount[32];

static uint16_t channel_to_mhz(uint8_t ch)
{
    if (ch <= 13) return 2407 + ch * 5;
    /* 5 GHz UNII bands */
    if (ch >= 36)  return 5000 + ch * 5;
    return 2412;
}

esp_err_t pcap_init(const char *mount_point)
{
    strncpy(s_mount, mount_point, sizeof(s_mount) - 1);
    s_mounted = true;
    ESP_LOGI(TAG, "PCAP writer ready, mount=%s", s_mount);
    return ESP_OK;
}

esp_err_t pcap_open(const char *filename)
{
    if (!s_mounted) return ESP_ERR_INVALID_STATE;
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", s_mount, filename);

    s_fp = fopen(path, "wb");
    if (!s_fp) { ESP_LOGE(TAG, "Cannot open %s", path); return ESP_FAIL; }

    pcap_global_hdr_t gh = {
        .magic_number  = 0xa1b2c3d4,
        .version_major = 2,
        .version_minor = 4,
        .thiszone      = 0,
        .sigfigs       = 0,
        .snaplen       = 65535,
        .network       = 127  /* radiotap */
    };
    fwrite(&gh, sizeof(gh), 1, s_fp);
    fflush(s_fp);
    ESP_LOGI(TAG, "PCAP opened: %s", path);
    return ESP_OK;
}

esp_err_t pcap_write_frame(const uint8_t *data, uint16_t len, int8_t rssi, uint8_t channel)
{
    if (!s_fp) return ESP_ERR_INVALID_STATE;

    uint64_t us = (uint64_t)esp_timer_get_time();

    radiotap_hdr_t rt = {
        .it_version  = 0,
        .it_pad      = 0,
        .it_len      = sizeof(radiotap_hdr_t),
        .it_present  = (1<<0)|(1<<1)|(1<<2)|(1<<3)|(1<<5),
        .tsft        = us,
        .flags       = 0,
        .rate        = 0x02,
        .channel_freq  = channel_to_mhz(channel),
        .channel_flags = (channel > 14) ? 0x0140 : 0x0080,
        .dbm_antsignal = rssi,
        ._pad        = 0
    };

    uint32_t total_len = sizeof(rt) + len;
    pcap_rec_hdr_t rh = {
        .ts_sec  = (uint32_t)(us / 1000000),
        .ts_usec = (uint32_t)(us % 1000000),
        .incl_len = total_len,
        .orig_len = total_len
    };

    fwrite(&rh, sizeof(rh), 1, s_fp);
    fwrite(&rt, sizeof(rt), 1, s_fp);
    fwrite(data, len, 1, s_fp);
    return ESP_OK;
}

esp_err_t pcap_close(void)
{
    if (s_fp) { fclose(s_fp); s_fp = NULL; }
    return ESP_OK;
}

esp_err_t pcap_deinit(void)
{
    pcap_close();
    s_mounted = false;
    return ESP_OK;
}
