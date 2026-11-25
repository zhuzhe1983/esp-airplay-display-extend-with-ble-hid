/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef enum {DISPLAY_SMOOTH=0,DISPLAY_SHARP=1} display_profile_t;
typedef struct {uint16_t width,height;uint8_t fps;} display_profile_config_t;
void display_profile_init(void);
display_profile_t display_profile_get(void);
esp_err_t display_profile_set(display_profile_t profile);
display_profile_config_t display_profile_config(display_profile_t profile);
const char *display_profile_name(display_profile_t profile);
