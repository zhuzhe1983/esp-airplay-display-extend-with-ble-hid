#pragma once
#include <assert.h>
typedef int esp_err_t;
enum { ESP_OK=0, ESP_ERR_NO_MEM=1, ESP_ERR_INVALID_STATE=2, ESP_ERR_INVALID_SIZE=3,
       ESP_ERR_NVS_NO_FREE_PAGES=4, ESP_ERR_NVS_NEW_VERSION_FOUND=5, ESP_ERR_INVALID_ARG=6 };
#define ESP_ERROR_CHECK(e) assert((e)==ESP_OK)
static inline const char *esp_err_to_name(esp_err_t e){(void)e;return "stub";}
