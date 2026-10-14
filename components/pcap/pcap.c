#include "pcap.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "esp_spiffs.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "soc/soc_caps.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/unistd.h>
#include <sys/stat.h>

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
    uint32_t network;   /* 127 = IEEE 802.11 radiotap */
} pcap_global_hdr_t;

typedef struct {
    uint32_t ts_sec;
    uint32_t ts_usec;
    uint32_t incl_len;
    uint32_t orig_len;
} pcap_rec_hdr_t;

/* Radiotap header with TSFT, flags, rate, channel, antenna signal (RSSI), antenna */
typedef struct {
    uint8_t  it_version;
    uint8_t  it_pad;
    uint16_t it_len;
    uint32_t it_present;  /* flags: TSFT(0), FLAGS(1), RATE(2), CHANNEL(3), DBM_ANTSIGNAL(5), ANTENNA(11) */
    uint64_t tsft;
    uint8_t  flags;
    uint8_t  rate;        /* 0x02 = 1 Mbps */
    uint16_t channel_freq;
    uint16_t channel_flags;
    int8_t   dbm_antsignal;
    uint8_t  antenna;
} radiotap_hdr_t;
#pragma pack(pop)

#define PCAP_MAX_FRAME_LEN 1600

static FILE        *s_fp = NULL;
static bool         s_mounted = false;
static bool         s_is_sd = false;
static char         s_mount[32] = {0};
static char         s_active_file[128] = {0};
static uint32_t     s_frames_written = 0;
#if SOC_SDMMC_HOST_SUPPORTED
static sdmmc_card_t *s_card = NULL;
#endif

static uint16_t channel_to_mhz(uint8_t ch)
{
    if (ch >= 1 && ch <= 14) {
        if (ch == 14) return 2484;
        return 2407 + ch * 5;
    }
    /* 5 GHz UNII bands */
    if (ch >= 32 && ch <= 177) return 5000 + ch * 5;
    return 2412;
}

esp_err_t pcap_mount_auto(void)
{
    if (s_mounted) return ESP_OK;

#if SOC_SDMMC_HOST_SUPPORTED
    /* 1. Try SDMMC 1-bit mode (Guition JC3248W535EN: CLK=12, CMD=11, D0=13) */
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = 12;
    slot.cmd = 11;
    slot.d0  = 13;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mcfg, &s_card);
    if (ret == ESP_OK) {
        strncpy(s_mount, "/sdcard", sizeof(s_mount) - 1);
        s_mounted = true;
        s_is_sd = true;
        ESP_LOGI(TAG, "microSD card mounted at %s", s_mount);
        sdmmc_card_print_info(stdout, s_card);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "microSD mount failed (%s) — falling back to SPIFFS", esp_err_to_name(ret));
    s_card = NULL;
#endif

    /* 2. Fallback to SPIFFS */
    esp_vfs_spiffs_conf_t spiffs_cfg = {
        .base_path = "/spiffs",
        .partition_label = NULL, /* mounts first spiffs partition found */
        .max_files = 5,
        .format_if_mount_failed = true
    };
    esp_err_t sret = esp_vfs_spiffs_register(&spiffs_cfg);
    if (sret == ESP_OK) {
        strncpy(s_mount, "/spiffs", sizeof(s_mount) - 1);
        s_mounted = true;
        s_is_sd = false;
        ESP_LOGI(TAG, "SPIFFS mounted at %s", s_mount);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Storage mount failed (SPIFFS: %s)", esp_err_to_name(sret));
    return sret;
}

esp_err_t pcap_init(const char *mount_point)
{
    if (mount_point && mount_point[0] != '\0') {
        strncpy(s_mount, mount_point, sizeof(s_mount) - 1);
        s_mounted = true;
        ESP_LOGI(TAG, "PCAP mount set to %s", s_mount);
        return ESP_OK;
    }
    return pcap_mount_auto();
}

esp_err_t pcap_open(const char *filename)
{
    if (!s_mounted) {
        esp_err_t err = pcap_mount_auto();
        if (err != ESP_OK) return err;
    }

    if (s_fp) {
        pcap_close();
    }

    char path[128];
    if (filename && filename[0] == '/') {
        strncpy(path, filename, sizeof(path) - 1);
    } else {
        snprintf(path, sizeof(path), "%s/%s", s_mount,
                 (filename && filename[0]) ? filename : "capture.pcap");
    }
    path[sizeof(path) - 1] = '\0';

    s_fp = fopen(path, "wb");
    if (!s_fp) {
        ESP_LOGE(TAG, "Cannot open %s for PCAP writing", path);
        return ESP_FAIL;
    }

    /* Batch write buffer */
    static char s_iobuf[4096];
    setvbuf(s_fp, s_iobuf, _IOFBF, sizeof(s_iobuf));

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
    fsync(fileno(s_fp));

    strncpy(s_active_file, path, sizeof(s_active_file) - 1);
    s_frames_written = 0;
    ESP_LOGI(TAG, "PCAP recording opened: %s", path);
    return ESP_OK;
}

esp_err_t pcap_write_frame(const uint8_t *data, uint16_t cap_len, uint16_t orig_len,
                           int8_t rssi, uint8_t channel)
{
    if (!s_fp || !data || cap_len == 0) return ESP_ERR_INVALID_STATE;

    if (cap_len > PCAP_MAX_FRAME_LEN) cap_len = PCAP_MAX_FRAME_LEN;
    if (orig_len < cap_len) orig_len = cap_len;

    uint64_t us = (uint64_t)esp_timer_get_time();

    radiotap_hdr_t rt = {
        .it_version    = 0,
        .it_pad        = 0,
        .it_len        = sizeof(radiotap_hdr_t),
        .it_present    = (1<<0) | (1<<1) | (1<<2) | (1<<3) | (1<<5) | (1<<11),
        .tsft          = us,
        .flags         = 0x10, /* FCS included */
        .rate          = 0x02, /* 1 Mbps */
        .channel_freq  = channel_to_mhz(channel),
        .channel_flags = (channel > 14) ? 0x0140 : 0x0080,
        .dbm_antsignal = rssi,
        .antenna       = 0
    };

    pcap_rec_hdr_t rh = {
        .ts_sec   = (uint32_t)(us / 1000000),
        .ts_usec  = (uint32_t)(us % 1000000),
        .incl_len = (uint32_t)sizeof(rt) + cap_len,
        .orig_len = (uint32_t)sizeof(rt) + orig_len,
    };

    uint8_t rec[sizeof(rh) + sizeof(rt) + PCAP_MAX_FRAME_LEN];
    size_t off = 0;
    memcpy(rec + off, &rh, sizeof(rh)); off += sizeof(rh);
    memcpy(rec + off, &rt, sizeof(rt)); off += sizeof(rt);
    memcpy(rec + off, data, cap_len);   off += cap_len;

    size_t written = fwrite(rec, 1, off, s_fp);
    if (written != off) {
        ESP_LOGW(TAG, "short pcap write (%u/%u)", (unsigned)written, (unsigned)off);
        return ESP_FAIL;
    }

    s_frames_written++;
    if ((s_frames_written % 16) == 0) {
        fflush(s_fp);
    }
    return ESP_OK;
}

esp_err_t pcap_write_packet(const void *payload, uint16_t len, int8_t rssi, uint8_t channel)
{
    return pcap_write_frame((const uint8_t *)payload, len, len, rssi, channel);
}

esp_err_t pcap_flush(void)
{
    if (s_fp) {
        fflush(s_fp);
        fsync(fileno(s_fp));
    }
    return ESP_OK;
}

esp_err_t pcap_close(void)
{
    if (s_fp) {
        fflush(s_fp);
        fsync(fileno(s_fp));
        fclose(s_fp);
        s_fp = NULL;
        ESP_LOGI(TAG, "PCAP closed: %lu frames saved to %s",
                 (unsigned long)s_frames_written, s_active_file);
    }
    return ESP_OK;
}

esp_err_t pcap_deinit(void)
{
    pcap_close();
#if SOC_SDMMC_HOST_SUPPORTED
    if (s_card) {
        esp_vfs_fat_sdcard_unmount(s_mount, s_card);
        s_card = NULL;
    }
#endif
    if (s_mounted && !s_is_sd) {
        esp_vfs_spiffs_unregister(NULL);
    }
    s_mounted = false;
    s_is_sd = false;
    s_mount[0] = '\0';
    s_active_file[0] = '\0';
    return ESP_OK;
}

bool pcap_is_mounted(void)       { return s_mounted; }
bool pcap_is_active(void)        { return s_fp != NULL; }
bool pcap_is_sd(void)            { return s_is_sd; }
const char *pcap_get_mount_point(void) { return s_mount; }
const char *pcap_get_active_file(void) { return s_active_file; }
uint32_t pcap_frame_count(void)  { return s_frames_written; }
