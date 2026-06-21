#include "gps.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "gps";

static int      s_uart_num  = -1;
static gps_fix_t s_fix      = {0};
static bool     s_running   = false;
static TaskHandle_t s_task  = NULL;

/* NMEA $GPRMC / $GPGGA minimal parser */
static void parse_gprmc(char *sentence)
{
    /* $GPRMC,time,status,lat,N/S,lon,E/W,speed,track,date,... */
    char *tok = strtok(sentence, ",");
    int field = 0;
    double lat = 0, lon = 0;
    char ns = 'N', ew = 'E';
    char status = 'V';
    float speed = 0;

    while (tok) {
        switch (field) {
            case 2: status = tok[0]; break;
            case 3: lat = atof(tok); break;
            case 4: ns  = tok[0]; break;
            case 5: lon = atof(tok); break;
            case 6: ew  = tok[0]; break;
            case 7: speed = atof(tok); break;
        }
        tok = strtok(NULL, ",");
        field++;
    }

    if (status == 'A') {
        /* Convert DDMM.MMMM to decimal degrees */
        int lat_deg = (int)(lat / 100);
        double lat_min = lat - lat_deg * 100;
        s_fix.lat = lat_deg + lat_min / 60.0;
        if (ns == 'S') s_fix.lat = -s_fix.lat;

        int lon_deg = (int)(lon / 100);
        double lon_min = lon - lon_deg * 100;
        s_fix.lon = lon_deg + lon_min / 60.0;
        if (ew == 'W') s_fix.lon = -s_fix.lon;

        s_fix.speed_knots = speed;
        s_fix.valid = true;
        s_fix.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
    } else {
        s_fix.valid = false;
    }
}

static void gps_task(void *arg)
{
    uint8_t buf[256];
    char line[128];
    int  line_pos = 0;

    while (s_running) {
        int n = uart_read_bytes(s_uart_num, buf, sizeof(buf) - 1, pdMS_TO_TICKS(100));
        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];
            if (c == '\n') {
                line[line_pos] = '\0';
                if (strncmp(line, "$GPRMC", 6) == 0 ||
                    strncmp(line, "$GNRMC", 6) == 0) {
                    parse_gprmc(line + 7);
                }
                line_pos = 0;
            } else if (c != '\r' && line_pos < (int)sizeof(line) - 1) {
                line[line_pos++] = c;
            }
        }
    }
    vTaskDelete(NULL);
}

esp_err_t gps_init(int tx_pin, int rx_pin, int uart_num)
{
    s_uart_num = uart_num;
    uart_config_t cfg = {
        .baud_rate  = 9600,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };
    ESP_ERROR_CHECK(uart_param_config(uart_num, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(uart_num, tx_pin, rx_pin,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(uart_num, 1024, 0, 0, NULL, 0));

    s_running = true;
    xTaskCreate(gps_task, "gps", 3072, NULL, 4, &s_task);
    ESP_LOGI(TAG, "GPS initialized on UART%d (TX=%d RX=%d)", uart_num, tx_pin, rx_pin);
    return ESP_OK;
}

esp_err_t gps_deinit(void)
{
    s_running = false;
    if (s_task) { vTaskDelay(pdMS_TO_TICKS(200)); s_task = NULL; }
    uart_driver_delete(s_uart_num);
    return ESP_OK;
}

bool gps_get_fix(gps_fix_t *out)
{
    if (!out) return false;
    *out = s_fix;
    return s_fix.valid;
}

bool gps_has_fix(void) { return s_fix.valid; }
