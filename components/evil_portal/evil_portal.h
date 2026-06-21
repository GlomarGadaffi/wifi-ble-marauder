#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t evil_portal_start(const char *ssid, const char *redirect_url);
esp_err_t evil_portal_stop(void);
uint32_t  evil_portal_client_count(void);

#ifdef __cplusplus
}
#endif
