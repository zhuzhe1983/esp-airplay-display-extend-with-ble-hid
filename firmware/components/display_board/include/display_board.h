#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t width;
    uint16_t height;
    const char *name;
} board_info_t;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t strength;
    uint8_t id;  /* Index in this sample; not a persistent controller tracking ID. */
} board_touch_point_t;

/** Initialize the selected board before starting ESP-Hosted or rendering tasks.
 * Call once from the startup task. A failed initialization requires a reboot.
 */
esp_err_t board_init(void);

/** Immutable native orientation, available before board_init(). */
const board_info_t *board_info(void);

/** Present one complete native-size image in host-endian RGB565.
 * stride_bytes is the distance in bytes between source row starts, >= width*2.
 * The function copies into board-owned scanout memory, flushes its cache, then
 * waits for two refresh boundaries before retiring the previous scanout buffer.
 * The caller may reuse its pixels after return, including on error. Calls are
 * serialized. No rotation or scaling is performed here.
 */
esp_err_t board_present_rgb565(const uint16_t *pixels, uint16_t width,
                              uint16_t height, size_t stride_bytes);

/** Poll touch in the same native coordinate system as board_info().
 * Returns number of points, 0 for released, or -1 on an I2C/controller error.
 * max_points may be at most 5; larger capacities are accepted and clamped.
 */
int board_read_touch(board_touch_point_t *points, size_t max_points);

/** Set backlight percentage (0..100). */
esp_err_t board_set_backlight(uint8_t percent);

#ifdef __cplusplus
}
#endif
