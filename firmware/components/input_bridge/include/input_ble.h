/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef enum { INPUT_BLE_OFF,INPUT_BLE_PAIRING,INPUT_BLE_CONNECTED,INPUT_BLE_READY,INPUT_BLE_ERROR } input_ble_state_t;
typedef void (*input_ble_state_cb)(input_ble_state_t,const char *detail,void *);
esp_err_t input_ble_start(const char *name,input_ble_state_cb cb,void *ctx);
bool input_ble_send(uint8_t report,const uint8_t *data,size_t len,void *unused);
void input_ble_pairing_window(void);
bool input_ble_is_ready(void);
uint8_t input_ble_keyboard_leds(void);
