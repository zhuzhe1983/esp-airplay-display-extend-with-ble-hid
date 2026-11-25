/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AIRPLAY_RECEIVER_STOPPED = 0,
    AIRPLAY_RECEIVER_ADVERTISING,
    AIRPLAY_RECEIVER_CONNECTING,
    AIRPLAY_RECEIVER_WAITING_VIDEO,
    AIRPLAY_RECEIVER_STREAMING,
    AIRPLAY_RECEIVER_NO_VIDEO,
    AIRPLAY_RECEIVER_ERROR,
} airplay_receiver_state_t;

typedef struct {
    const uint8_t *data; /* Complete H.264 Annex B access unit; SPS/PPS prefix on IDRs. */
    size_t len;
    uint64_t pts_us;     /* Sender presentation timestamp relative to this session's first AU. */
    bool keyframe;
} airplay_receiver_frame_t;

typedef struct {
    const char *name;    /* UTF-8 Bonjour instance name; copied by start(), maximum 48 bytes. */
    uint16_t width;      /* Requested stream size. AirPlay senders may choose a different size. */
    uint16_t height;
    uint8_t max_fps;     /* Requested maximum FPS; not a guarantee. */
    uint16_t width_mm, height_mm; /* Physical display size; both zero if unknown. */
    uint8_t profile_id; /* 0: hardware identity, 1: distinct local virtual identity. */
    void (*frame_cb)(const airplay_receiver_frame_t *frame, void *ctx);
    void (*state_cb)(airplay_receiver_state_t state, const char *detail, void *ctx);
    void *ctx;
} airplay_receiver_config_t;

/**
 * Start one LAN AirPlay mirroring receiver after NVS and the IP interface are ready.
 * The receiver persists its Ed25519 identity in NVS namespace "p4_airplay".
 * Callbacks run on receiver tasks and may run concurrently. State callbacks must
 * return promptly; frame callbacks may apply bounded TCP backpressure (up to 2s).
 * Frame data and state detail strings are valid only for the duration of the callback.
 * Copy frames synchronously into a bounded decoder queue; never retain the pointer.
 * Do not call start/stop from a callback or call these lifecycle functions concurrently.
 * STREAMING means a valid decrypted AU arrived; only the decoder can establish a displayed image.
 * This is an experimental legacy AirPlay receiver, not Sidecar or certified AirPlay.
 */
esp_err_t airplay_receiver_start(const airplay_receiver_config_t *config);

/** Stop network tasks and remove this component's Bonjour services; waits up to 6 seconds. */
esp_err_t airplay_receiver_stop(void);
airplay_receiver_state_t airplay_receiver_get_state(void);
const char *airplay_receiver_state_name(airplay_receiver_state_t state);

#ifdef __cplusplus
}
#endif
