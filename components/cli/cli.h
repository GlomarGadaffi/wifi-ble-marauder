#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cli_init(void);
void      cli_task(void *arg);

#ifdef __cplusplus
}
#endif
