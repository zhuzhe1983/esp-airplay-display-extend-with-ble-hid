/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "airplay_receiver.h"
#include "ap_internal.h"
#include "h264_packet.h"

#include <stdatomic.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "nvs.h"
#include "sodium.h"

static const char *TAG = "airplay";
static airplay_receiver_config_t s_config;
static char s_name[49];
static bool s_started;
static atomic_int s_state = AIRPLAY_RECEIVER_STOPPED;
static uint8_t s_codec[AP_CODEC_MAX];
static size_t s_codec_len;
static uint8_t *s_prefixed;
static size_t s_prefixed_cap;
static bool s_wait_idr = true, s_have_pts;
static uint64_t s_first_pts;

const char *airplay_receiver_state_name(airplay_receiver_state_t state) {
    static const char *const names[] = {
        "stopped", "advertising", "connecting", "waiting-video", "streaming", "no-video", "error"
    };
    return (unsigned)state < sizeof names / sizeof names[0] ? names[state] : "unknown";
}

airplay_receiver_state_t airplay_receiver_get_state(void) {
    return (airplay_receiver_state_t)atomic_load(&s_state);
}

void ap_receiver_notify(airplay_receiver_state_t state, const char *detail) {
    int previous = atomic_exchange(&s_state, state);
    if (previous == (int)state) return;
    ESP_LOGI(TAG, "%s: %s", airplay_receiver_state_name(state), detail ? detail : "");
    if (s_config.state_cb) s_config.state_cb(state, detail ? detail : "", s_config.ctx);
}

void ap_receiver_new_session(void) {
    s_codec_len = 0;
    s_wait_idr = true;
    s_have_pts = false;
    s_first_pts = 0;
}

static void receiver_log(const char *msg) { ESP_LOGI(TAG, "%s", msg); }

static void receiver_config(const uint8_t *data, size_t len) {
    if (!data || !len || len > sizeof s_codec) {
        s_codec_len = 0;
        s_wait_idr = true;
        ap_receiver_notify(AIRPLAY_RECEIVER_ERROR, "Invalid or oversized H.264 codec configuration");
        return;
    }
    if (len != s_codec_len || memcmp(data, s_codec, len)) {
        s_wait_idr = true;
        ap_receiver_notify(AIRPLAY_RECEIVER_WAITING_VIDEO, "H.264 configuration changed; waiting for a new IDR");
    }
    memcpy(s_codec, data, len);
    s_codec_len = len;
}

static void receiver_lost(void) {
    s_wait_idr = true;
    ap_receiver_notify(AIRPLAY_RECEIVER_WAITING_VIDEO, "Lost or invalid H.264 packet; waiting for a new IDR");
}

static void receiver_au(const uint8_t *data, size_t len, bool idr, uint64_t timestamp_us) {
    if (!data || !len || len > AP_MAX_PAYLOAD || !s_codec_len || (s_wait_idr && !idr)) return;
    airplay_receiver_frame_t frame = {.data = data, .len = len, .keyframe = idr};
    if (idr) {
        size_t total = s_codec_len + len;
        if (total > s_prefixed_cap) {
            uint8_t *buffer = heap_caps_realloc(s_prefixed, total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!buffer) {
                receiver_lost();
                ap_receiver_notify(AIRPLAY_RECEIVER_ERROR, "Not enough PSRAM for SPS/PPS plus IDR");
                return;
            }
            s_prefixed = buffer;
            s_prefixed_cap = total;
        }
        memcpy(s_prefixed, s_codec, s_codec_len);
        memcpy(s_prefixed + s_codec_len, data, len);
        frame.data = s_prefixed;
        frame.len = total;
        s_wait_idr = false;
    }
    if (!s_have_pts) { s_first_pts = timestamp_us; s_have_pts = true; }
    frame.pts_us = timestamp_us >= s_first_pts ? timestamp_us - s_first_pts : 0;
    /* This event is transport validation, not evidence that the screen rendered a frame. */
    ap_receiver_notify(AIRPLAY_RECEIVER_STREAMING, "Received decrypted H.264 access units; decoder output is separate");
    s_config.frame_cb(&frame, s_config.ctx);
}

static esp_err_t load_identity(uint8_t seed[32], uint8_t profile_id) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("p4_airplay", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    size_t len = 32;
    const char *key=profile_id ? "smooth_seed" : "ed25519_seed";
    err = nvs_get_blob(nvs, key, seed, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        esp_fill_random(seed, 32);
        err = nvs_set_blob(nvs, key, seed, 32);
        if (err == ESP_OK) err = nvs_commit(nvs);
    } else if (err == ESP_OK && len != 32) {
        err = ESP_ERR_INVALID_SIZE;
    }
    nvs_close(nvs);
    return err;
}

esp_err_t airplay_receiver_start(const airplay_receiver_config_t *config) {
    if (s_started) return ESP_ERR_INVALID_STATE;
    if (!config || !config->name || !config->frame_cb ||
        !config->width || !config->height || config->width > 1920 || config->height > 1920 ||
        !config->max_fps || config->max_fps > 60 ||
        config->profile_id > 1 || config->width_mm > 2000 || config->height_mm > 2000 ||
        (!!config->width_mm != !!config->height_mm)) return ESP_ERR_INVALID_ARG;
    size_t name_len = strnlen(config->name, sizeof s_name);
    if (!name_len || name_len >= sizeof s_name) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < name_len; ++i)
        if ((unsigned char)config->name[i] < 0x20 || config->name[i] == 0x7f) return ESP_ERR_INVALID_ARG;
    s_config = *config;
    memcpy(s_name, config->name, name_len + 1);
    s_config.name = s_name;
    ap_receiver_new_session();
    airplay_cfg cfg = {
        .name = s_name, .width = config->width, .height = config->height, .max_fps = config->max_fps,
        .width_mm = config->width_mm, .height_mm = config->height_mm,
        .profile_id=config->profile_id,
        .on_config = receiver_config, .on_au = receiver_au, .on_lost = receiver_lost, .log = receiver_log,
    };
    esp_err_t err = esp_efuse_mac_get_default(cfg.hwaddr);
    if(err==ESP_OK&&config->profile_id){
        /* macOS caches endpoint display capabilities by AirPlay device ID.
         * Give the smooth virtual receiver its own unicast/local identity;
         * neither the Wi-Fi interface nor the BLE controller address changes. */
        cfg.hwaddr[0]=(cfg.hwaddr[0]&0xfc)|0x02;
        cfg.hwaddr[5]^=config->profile_id;
    }
    if (err == ESP_OK) err = load_identity(cfg.ed25519_seed,config->profile_id);
    if (err != ESP_OK) {
        ap_receiver_notify(AIRPLAY_RECEIVER_ERROR, "Cannot load persistent receiver identity; initialize NVS first");
        return err;
    }
    bool started = airplay_start(&cfg);
    sodium_memzero(cfg.ed25519_seed, sizeof cfg.ed25519_seed);
    if (!started) {
        ap_receiver_notify(AIRPLAY_RECEIVER_ERROR, "Cannot start receiver sockets, crypto, mDNS, or task");
        return ESP_FAIL;
    }
    s_started = true;
    if (airplay_receiver_get_state() == AIRPLAY_RECEIVER_STOPPED ||
        airplay_receiver_get_state() == AIRPLAY_RECEIVER_ERROR)
        ap_receiver_notify(AIRPLAY_RECEIVER_ADVERTISING, "LAN service available; no video session yet");
    return ESP_OK;
}

esp_err_t airplay_receiver_stop(void) {
    if (!s_started) return ESP_OK;
    if (!airplay_stop()) {
        ap_receiver_notify(AIRPLAY_RECEIVER_ERROR, "Receiver task did not stop within its deadline");
        return ESP_ERR_TIMEOUT;
    }
    s_started = false;
    heap_caps_free(s_prefixed);
    s_prefixed = NULL;
    s_prefixed_cap = 0;
    ap_receiver_new_session();
    sodium_memzero(ap_sk, sizeof ap_sk);
    sodium_memzero(ap_cfg.ed25519_seed, sizeof ap_cfg.ed25519_seed);
    ap_receiver_notify(AIRPLAY_RECEIVER_STOPPED, "Receiver stopped");
    memset(&s_config, 0, sizeof s_config);
    return ESP_OK;
}
