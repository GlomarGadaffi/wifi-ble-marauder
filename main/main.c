#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_timer.h"

#include "wifi_attack.h"
#include "ble_attack.h"
#include "cli.h"

/* Board selection — override via CMake define or sdkconfig */
#if defined(CONFIG_BOARD_C5) || defined(BOARD_C5)
  #include "board_c5.h"
#elif defined(CONFIG_BOARD_C6) || defined(BOARD_C6)
  #include "board_c6.h"
#elif defined(CONFIG_BOARD_S3_GUITION) || defined(BOARD_S3_GUITION)
  #include "board_s3_guition.h"
  #include "display.h"
  #include "spectrum.h"
  static bm_display_t s_display;
#else
  #include "board_c5.h"
#endif

static const char *TAG = "binary-mist";

static void health_task(void *arg)
{
    while (1) {
        ESP_LOGI(TAG, "heap=%lu  psram=%lu  wifi_tx=%lu  ble_tx=%lu",
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)esp_get_free_internal_heap_size(),
                 (unsigned long)wifi_attack_packets_sent(),
                 (unsigned long)ble_attack_frames_sent());
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "binary-mist starting on %s", BOARD_NAME);
    ESP_LOGI(TAG, "IDF version: %s", esp_get_idf_version());

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (wifi_attack_has_5ghz()) {
        ESP_LOGI(TAG, "5 GHz WiFi supported — dual-band attacks available");
    } else {
        ESP_LOGI(TAG, "2.4 GHz only");
    }

#if defined(CONFIG_BOARD_S3_GUITION) || defined(BOARD_S3_GUITION)
    /* Phase 4: display + RSSI spectrum visualizer (core 1, priority 4).
     * spectrum_task starts promiscuous scan and handles touch-dispatched attacks.
     * The UART CLI still works in parallel on the same core via the REPL task. */
    ESP_ERROR_CHECK(bm_display_init(&s_display));
    xTaskCreatePinnedToCore(spectrum_task, "spectrum", 12288, &s_display, 4, NULL, 1);
#endif

    cli_init();
    xTaskCreatePinnedToCore(cli_task, "cli", 8192, NULL, 5, NULL, 1);

    xTaskCreatePinnedToCore(health_task, "health", 2048, NULL, 2, NULL, 1);
}
