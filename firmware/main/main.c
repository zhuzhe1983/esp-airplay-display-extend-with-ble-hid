/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "airplay_receiver.h"
#include "video_decoder.h"
#include "display_board.h"
#include "input_ble.h"
#include "app_network.h"
#include "native_ui.h"
#include "display_profile.h"

#define MAX_ACCESS_UNIT_BYTES (1024U * 1024U)
#define INGRESS_WAIT_US 2000000
static const char *TAG = "native_display";
typedef struct { uint8_t *data; size_t len; unsigned epoch; int64_t received_us; } access_unit_t;
static QueueHandle_t frame_queue;
static SemaphoreHandle_t pipeline_lock;
static TaskHandle_t decode_task_handle;
static atomic_uint epoch = 1;
/* Both counts are protected by pipeline_lock. The budget includes the AU
 * currently being decoded, so a full queue cannot exceed it by one large AU. */
static size_t queued_bytes, inflight_bytes;
static bool needs_idr = true;
static uint32_t dropped;
static video_decoder_t *decoder;
typedef struct { unsigned current_epoch; int64_t last_present_us; } decode_context_t;
static decode_context_t decode_ctx;

static void count_drop_locked(void) {
    if (dropped != UINT32_MAX) ++dropped;
}

static void flush_locked(void) {
    access_unit_t queued;
    while (xQueueReceive(frame_queue, &queued, 0) == pdTRUE) {
        free(queued.data);
        count_drop_locked();
    }
    queued_bytes = 0;
    needs_idr = true;
    atomic_fetch_add(&epoch, 1);
}

static void reset_pipeline(void) {
    if (!pipeline_lock) return;
    xSemaphoreTake(pipeline_lock, portMAX_DELAY);
    flush_locked();
    const uint32_t count = dropped;
    xSemaphoreGive(pipeline_lock);
    native_ui_queue_drop(count);
}

/* Lock ordering is pipeline -> UI. UI callbacks must never call back into the
 * receiver/pipeline while holding their mutex. This also makes a reset and an
 * obsolete decoder callback mutually exclusive at the presentation boundary. */
static void decoder_failed(unsigned failed_epoch, int error) {
    xSemaphoreTake(pipeline_lock, portMAX_DELAY);
    if (failed_epoch == atomic_load(&epoch)) {
        flush_locked();
        native_ui_decoder_error(error);
        native_ui_queue_drop(dropped);
    }
    xSemaphoreGive(pipeline_lock);
}

static void received_frame(const airplay_receiver_frame_t *frame, void *ctx) {
    (void)ctx;
    bool enqueued = false;
    xSemaphoreTake(pipeline_lock, portMAX_DELAY);
    if (!frame || !frame->data || !frame->len ||
        frame->len > MAX_ACCESS_UNIT_BYTES || frame->len > CONFIG_P4_FRAME_QUEUE_BYTES) {
        flush_locked();
        count_drop_locked();
        goto done;
    }
    const unsigned arrival_epoch = atomic_load(&epoch);
    const int64_t wait_started = esp_timer_get_time();
    while (queued_bytes + inflight_bytes + frame->len > CONFIG_P4_FRAME_QUEUE_BYTES ||
           uxQueueSpacesAvailable(frame_queue) == 0) {
        /* The source uses TCP. Preserve predictive frames by letting its receive
         * window close while the decoder catches up, rather than discarding a
         * reference chain and waiting indefinitely for another IDR. No mutex is
         * held while waiting, and a reset cancels this old session's pending AU. */
        if (esp_timer_get_time() - wait_started >= INGRESS_WAIT_US) break;
        xSemaphoreGive(pipeline_lock);
        vTaskDelay(pdMS_TO_TICKS(5));
        xSemaphoreTake(pipeline_lock, portMAX_DELAY);
        if (arrival_epoch != atomic_load(&epoch)) {
            count_drop_locked();
            goto done;
        }
    }
    if (queued_bytes + inflight_bytes + frame->len > CONFIG_P4_FRAME_QUEUE_BYTES ||
        uxQueueSpacesAvailable(frame_queue) == 0) {
        /* Losing any predictive AU invalidates the following P/B pictures. */
        flush_locked();
    }
    if ((needs_idr && !frame->keyframe) ||
        inflight_bytes + frame->len > CONFIG_P4_FRAME_QUEUE_BYTES) {
        count_drop_locked();
        goto done;
    }
    access_unit_t unit = {.len = frame->len, .epoch = atomic_load(&epoch), .received_us=esp_timer_get_time()};
    unit.data = heap_caps_malloc(unit.len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!unit.data) {
        flush_locked();
        count_drop_locked();
        goto done;
    }
    memcpy(unit.data, frame->data, unit.len);
    if (xQueueSend(frame_queue, &unit, 0) == pdTRUE) {
        queued_bytes += unit.len;
        needs_idr = false;
        enqueued = true;
    } else {
        free(unit.data);
        flush_locked();
        count_drop_locked();
    }
 done:;
    const uint32_t count = dropped;
    xSemaphoreGive(pipeline_lock);
    if (enqueued) xTaskNotifyGive(decode_task_handle);
    native_ui_queue_drop(count);
}

static bool state_resets_video(airplay_receiver_state_t state) {
    return state == AIRPLAY_RECEIVER_CONNECTING ||
           state == AIRPLAY_RECEIVER_WAITING_VIDEO ||
           state == AIRPLAY_RECEIVER_ADVERTISING ||
           state == AIRPLAY_RECEIVER_STOPPED ||
           state == AIRPLAY_RECEIVER_NO_VIDEO ||
           state == AIRPLAY_RECEIVER_ERROR;
}

static void receiver_state(airplay_receiver_state_t state, const char *detail, void *ctx) {
    if (state_resets_video(state) && pipeline_lock) {
        xSemaphoreTake(pipeline_lock, portMAX_DELAY);
        flush_locked();
        native_ui_airplay(state, detail, ctx);
        native_ui_queue_drop(dropped);
        xSemaphoreGive(pipeline_lock);
    } else {
        native_ui_airplay(state, detail, ctx);
    }
    ESP_LOGI(TAG, "AirPlay %s: %s", airplay_receiver_state_name(state), detail ? detail : "");
}

static void decoded_frame(void *ctx, const video_decoder_frame_t *frame) {
    decode_context_t *context = ctx;
    xSemaphoreTake(pipeline_lock, portMAX_DELAY);
    if (context->current_epoch == atomic_load(&epoch)) {
        native_ui_video(NULL, frame);
        context->last_present_us=esp_timer_get_time();
    }
    xSemaphoreGive(pipeline_lock);
}

static bool should_present_frame(void *ctx){
    decode_context_t *context=ctx;
    xSemaphoreTake(pipeline_lock,portMAX_DELAY);
    bool current=context->current_epoch==atomic_load(&epoch);
    bool backlog=uxQueueMessagesWaiting(frame_queue)>0;
    xSemaphoreGive(pipeline_lock);
    if(!current)return false;
    int64_t now=esp_timer_get_time();
    if(backlog&&context->last_present_us&&now-context->last_present_us<150000)return false;
    return true;
}

/* Every queue mutation is under the same mutex as its byte accounting. Task
 * notifications wake this consumer without blocking on a queue while locked. */
static bool take_access_unit(access_unit_t *unit) {
    xSemaphoreTake(pipeline_lock, portMAX_DELAY);
    const bool found = xQueueReceive(frame_queue, unit, 0) == pdTRUE;
    if (found) {
        queued_bytes -= unit->len;
        inflight_bytes = unit->len;
    }
    xSemaphoreGive(pipeline_lock);
    return found;
}

static void retire_access_unit(access_unit_t *unit) {
    free(unit->data);
    unit->data = NULL;
    xSemaphoreTake(pipeline_lock, portMAX_DELAY);
    inflight_bytes = 0;
    xSemaphoreGive(pipeline_lock);
}

static void decode_task(void *arg) {
    (void)arg;
    access_unit_t unit;
    unsigned seen_epoch = 0;
    bool initialized_for_epoch = false;
    unsigned perf_count=0;
    for (;;) {
        if (!take_access_unit(&unit)) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        if (unit.epoch != atomic_load(&epoch)) {
            retire_access_unit(&unit);
            continue;
        }
        if (!initialized_for_epoch || seen_epoch != unit.epoch) {
            const int error = video_decoder_reset(decoder);
            if (error != VIDEO_DECODER_OK) {
                initialized_for_epoch = false;
                retire_access_unit(&unit);
                ESP_LOGE(TAG, "Decoder reset: %s", video_decoder_error_string(error));
                decoder_failed(unit.epoch, error);
                vTaskDelay(pdMS_TO_TICKS(250));
                continue;
            }
            seen_epoch = unit.epoch;
            decode_ctx.last_present_us=0;
            initialized_for_epoch = true;
        }
        /* Reset may have taken long enough for the network to end this epoch. */
        if (unit.epoch != atomic_load(&epoch)) {
            retire_access_unit(&unit);
            continue;
        }
        decode_ctx.current_epoch = unit.epoch;
        if(++perf_count%30==0){
            xSemaphoreTake(pipeline_lock,portMAX_DELAY);
            unsigned count=(unsigned)uxQueueMessagesWaiting(frame_queue),bytes=(unsigned)queued_bytes;
            xSemaphoreGive(pipeline_lock);
            ESP_LOGI("pipeline_perf","queue_age=%lldus queued=%u bytes=%u",
                (long long)(esp_timer_get_time()-unit.received_us),count,bytes);
        }
        const int error = video_decoder_push(decoder, unit.data, unit.len);
        retire_access_unit(&unit);
        if (error != VIDEO_DECODER_OK) {
            ESP_LOGW(TAG, "Decoder rejected stream: %s", video_decoder_error_string(error));
            decoder_failed(unit.epoch, error);
        }
        /* Let idle/watchdog and input tasks run even when a sender overruns us. */
        vTaskDelay(1);
    }
}

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    display_profile_init();
    ESP_LOGI(TAG, "P4 Native Display 0.1.0 experimental; board %s", board_info()->name);
    err = board_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Board init failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_ERROR_CHECK(native_ui_start());
    frame_queue = xQueueCreate(2, sizeof(access_unit_t));
    pipeline_lock = xSemaphoreCreateMutex();
    if (!frame_queue || !pipeline_lock) {
        if (frame_queue) vQueueDelete(frame_queue);
        if (pipeline_lock) vSemaphoreDelete(pipeline_lock);
        frame_queue = NULL; pipeline_lock = NULL;
        native_ui_decoder_error(-100);
        return;
    }
    video_decoder_config_t video_config = {
        .panel_width = board_info()->width, .panel_height = board_info()->height,
        .max_input_width = 1280, .max_input_height = 1280,
        .max_access_unit_bytes = MAX_ACCESS_UNIT_BYTES,
        .callback = decoded_frame, .user = &decode_ctx, .should_present=should_present_frame
    };
    const int result = video_decoder_init(&video_config, &decoder);
    if (result != VIDEO_DECODER_OK) {
        native_ui_decoder_error(result);
        return;
    }
#if CONFIG_FREERTOS_UNICORE
    const BaseType_t decode_core = 0;
#else
    const BaseType_t decode_core = 1;
#endif
    if (xTaskCreatePinnedToCore(decode_task, "h264_decode", 32768, NULL, 3,
                                &decode_task_handle, decode_core) != pdPASS) {
        video_decoder_destroy(decoder); decoder = NULL;
        native_ui_decoder_error(-100);
        return;
    }
    err = app_network_start(board_info()->name, native_ui_network, NULL);
    if (err != ESP_OK) {
        native_ui_network(NETWORK_ERROR, "Wireless startup failed", esp_err_to_name(err), NULL);
        return;
    }
#if CONFIG_DISPLAY_BOARD_M5STACK_TAB5
    const char *ble_name = "M5Stack Tab5 Input";
#else
    const char *ble_name = "MetalioClaw4 Input";
#endif
    err = input_ble_start(ble_name, native_ui_ble, NULL);
    if (err != ESP_OK) native_ui_ble(INPUT_BLE_ERROR, "C5/C6 Wi-Fi + BLE firmware required", NULL);
    err = app_network_start_usb_provisioning();
    if (err != ESP_OK) ESP_LOGW(TAG, "USB provisioning unavailable: %s", esp_err_to_name(err));
    bool receiving = false;
    display_profile_t active_profile=display_profile_get();
    for (;;) {
        const bool online = app_network_ready();
        display_profile_t requested_profile=display_profile_get();
        if(receiving&&requested_profile!=active_profile){
            err=airplay_receiver_stop();
            if(err==ESP_OK){receiving=false;reset_pipeline();}
            else ESP_LOGW(TAG,"Mode switch stop pending: %s",esp_err_to_name(err));
        }
        if (online && !receiving) {
            active_profile=requested_profile;
            display_profile_config_t mode=display_profile_config(active_profile);
            char receiver_name[49];
            snprintf(receiver_name,sizeof receiver_name,"%.38s %s",app_network_device_name(),
                active_profile==DISPLAY_SMOOTH?"FastSync":"Sharp");
            ESP_LOGI(TAG,"Video mode %s: %ux%u maxFPS %u",display_profile_name(active_profile),mode.width,mode.height,mode.fps);
            airplay_receiver_config_t config = {
                .name = receiver_name, .width = mode.width,
                .height = mode.height, .max_fps = mode.fps,
                .width_mm = CONFIG_P4_DISPLAY_WIDTH_MM, .height_mm = CONFIG_P4_DISPLAY_HEIGHT_MM,
                .profile_id = active_profile==DISPLAY_SMOOTH?1:0,
                .frame_cb = received_frame, .state_cb = receiver_state
            };
            err = airplay_receiver_start(&config);
            receiving = err == ESP_OK;
            if (!receiving) {
                native_ui_airplay(AIRPLAY_RECEIVER_ERROR, esp_err_to_name(err), NULL);
                vTaskDelay(pdMS_TO_TICKS(4000));
            }
        } else if (!online && receiving) {
            err = airplay_receiver_stop();
            if (err == ESP_OK) {
                receiving = false;
                reset_pipeline();
            } else ESP_LOGW(TAG, "Receiver stop pending: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
