/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
typedef enum { NETWORK_STARTING,NETWORK_CONNECTING,NETWORK_READY,NETWORK_SETUP,NETWORK_ERROR } network_state_t;
typedef void (*network_state_cb)(network_state_t,const char *line1,const char *line2,void *ctx);
esp_err_t app_network_start(const char *default_name,network_state_cb cb,void *ctx);
const char *app_network_device_name(void);
bool app_network_ready(void);
void app_network_forget_wifi(void);
