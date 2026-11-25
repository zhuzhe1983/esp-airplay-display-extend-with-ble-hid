#include <string.h>
#include "sdkconfig.h"
#include "display_board.h"
#include "board_private.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_lcd_mipi_dsi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "display_board";
#if CONFIG_DISPLAY_BOARD_M5STACK_TAB5
static const board_info_t info = {720, 1280, "M5Stack Tab5"};
#elif CONFIG_CLAW4_LCD_FL7707N
static const board_info_t info = {720, 720, "MetalioClaw4 FL7707N"};
#else
static const board_info_t info = {720, 720, "MetalioClaw4 NV3051F"};
#endif

static board_hw_t hw;
static SemaphoreHandle_t render_lock;
static SemaphoreHandle_t touch_lock;
static SemaphoreHandle_t refresh_sem;
static uint32_t refresh_count;
static void *framebuffers[2];
static unsigned int next_framebuffer = 1;
static bool attempted;
static bool ready;
static bool scanout_failed;

static bool IRAM_ATTR on_refresh(esp_lcd_panel_handle_t panel,
                                esp_lcd_dpi_panel_event_data_t *event,
                                void *context)
{
    (void)panel;
    (void)event;
    (void)context;
    __atomic_add_fetch(&refresh_count, 1, __ATOMIC_RELEASE);
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(refresh_sem, &woken);
    return woken == pdTRUE;
}

const board_info_t *board_info(void)
{
    return &info;
}

esp_err_t board_init(void)
{
    if (ready) {
        return ESP_OK;
    }
    if (attempted) {
        return ESP_ERR_INVALID_STATE;
    }
    attempted = true;
    render_lock = xSemaphoreCreateMutex();
    touch_lock = xSemaphoreCreateMutex();
    refresh_sem = xSemaphoreCreateBinary();
    if (!render_lock || !touch_lock || !refresh_sem) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = board_hw_init(&hw);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Board initialization failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_lcd_dpi_panel_get_frame_buffer(hw.panel, 2, &framebuffers[0], &framebuffers[1]);
    if (err != ESP_OK) {
        return err;
    }
    const esp_lcd_dpi_panel_event_callbacks_t callbacks = {.on_refresh_done = on_refresh};
    err = esp_lcd_dpi_panel_register_event_callbacks(hw.panel, &callbacks, NULL);
    if (err != ESP_OK) {
        return err;
    }
    err = board_hw_backlight(CONFIG_DISPLAY_BOARD_BACKLIGHT_PERCENT);
    if (err != ESP_OK) {
        return err;
    }
    ready = true;
    ESP_LOGI(TAG, "%s, native %ux%u, two %u-bit scanout buffers",
             info.name, info.width, info.height, hw.scanout_bytes_per_pixel * 8);
    return ESP_OK;
}

esp_err_t board_present_rgb565(const uint16_t *pixels, uint16_t width,
                              uint16_t height, size_t stride_bytes)
{
    if (!pixels || width != info.width || height != info.height ||
        stride_bytes < (size_t)width * sizeof(uint16_t) ||
        (stride_bytes & 1) || ((uintptr_t)pixels & 1) ||
        stride_bytes > SIZE_MAX / height) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ready) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(render_lock, portMAX_DELAY);
    if (scanout_failed) {
        xSemaphoreGive(render_lock);
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t *dst = framebuffers[next_framebuffer];
    const uint8_t *src = (const uint8_t *)pixels;
    if (hw.scanout_bytes_per_pixel == 2) {
        for (unsigned int y = 0; y < height; ++y) {
            memcpy(dst + (size_t)y * width * 2, src + (size_t)y * stride_bytes, width * 2);
        }
    } else {
        // IDF's packed RGB888 format stores B, G, R bytes in increasing addresses.
        // Keep Claw's published RGB888 panel/DSI setup unchanged.
        for (unsigned int y = 0; y < height; ++y) {
            const uint16_t *row = (const uint16_t *)(src + (size_t)y * stride_bytes);
            for (unsigned int x = 0; x < width; ++x) {
                uint16_t pixel = row[x];
                uint8_t b = pixel & 31;
                uint8_t g = (pixel >> 5) & 63;
                uint8_t r = pixel >> 11;
                *dst++ = (uint8_t)((b << 3) | (b >> 2));
                *dst++ = (uint8_t)((g << 2) | (g >> 4));
                *dst++ = (uint8_t)((r << 3) | (r >> 2));
            }
        }
    }
    // A pointer returned by get_frame_buffer selects a buffer and performs cache
    // writeback in IDF 5.5.4. It does not retain the caller's source buffer.
    esp_err_t err = esp_lcd_panel_draw_bitmap(hw.panel, 0, 0, width, height,
                                             framebuffers[next_framebuffer]);
    if (err == ESP_OK) {
        // Measure AFTER the index change. Two subsequent VSYNC boundaries let
        // the current DMA frame end and the newly selected buffer scan out.
        // This deliberately favors safe buffer reuse over maximum frame rate.
        uint32_t before = __atomic_load_n(&refresh_count, __ATOMIC_ACQUIRE);
        TickType_t started = xTaskGetTickCount();
        const TickType_t limit = pdMS_TO_TICKS(250);
        while ((uint32_t)(__atomic_load_n(&refresh_count, __ATOMIC_ACQUIRE) - before) < 2) {
            TickType_t elapsed = xTaskGetTickCount() - started;
            if (elapsed >= limit || xSemaphoreTake(refresh_sem, limit - elapsed) != pdTRUE) {
                err = ESP_ERR_TIMEOUT;
                break;
            }
        }
    }
    if (err == ESP_OK) {
        next_framebuffer ^= 1;
    } else {
        // Without a completed refresh the retired framebuffer's ownership is
        // unknown. Fail closed until reboot instead of writing into live DMA.
        scanout_failed = true;
        ESP_LOGE(TAG, "Scanout failed: %s; rendering disabled until reboot", esp_err_to_name(err));
    }
    xSemaphoreGive(render_lock);
    return err;
}

int board_read_touch(board_touch_point_t *points, size_t max_points)
{
    if (!ready || !points || max_points == 0) {
        return -1;
    }
    esp_lcd_touch_point_data_t sample[5] = {0};
    uint8_t count = 0;
    uint8_t capacity = max_points > 5 ? 5 : (uint8_t)max_points;
    xSemaphoreTake(touch_lock, portMAX_DELAY);
    esp_err_t err = esp_lcd_touch_read_data(hw.touch);
    if (err == ESP_OK) {
        err = esp_lcd_touch_get_data(hw.touch, sample, &count, capacity);
    }
    xSemaphoreGive(touch_lock);
    if (err != ESP_OK) {
        return -1;
    }
    for (uint8_t i = 0; i < count; ++i) {
        points[i] = (board_touch_point_t){
            .x = sample[i].x < info.width ? sample[i].x : info.width - 1,
            .y = sample[i].y < info.height ? sample[i].y : info.height - 1,
            .strength = sample[i].strength,
            .id = i,
        };
    }
    return count;
}

esp_err_t board_set_backlight(uint8_t percent)
{
    if (percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ready) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(render_lock, portMAX_DELAY);
    esp_err_t err = board_hw_backlight(percent);
    xSemaphoreGive(render_lock);
    return err;
}
