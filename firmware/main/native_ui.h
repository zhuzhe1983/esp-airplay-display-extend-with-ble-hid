/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "esp_err.h"
#include "video_decoder.h"
#include "airplay_receiver.h"
#include "input_ble.h"
#include "app_network.h"
esp_err_t native_ui_start(void);
void native_ui_network(network_state_t,const char *,const char *,void *);
void native_ui_airplay(airplay_receiver_state_t,const char *,void *);
void native_ui_ble(input_ble_state_t,const char *,void *);
/* Synchronously copies a decoded frame. The UI's DECODED counter records this
 * acceptance; SHOWN advances only when board_present_rgb565 succeeds for a new
 * video frame. Neither counter certifies macOS/physical-panel compatibility. */
void native_ui_video(void *,const video_decoder_frame_t *);
void native_ui_decoder_error(int error);
void native_ui_queue_drop(uint32_t total);
