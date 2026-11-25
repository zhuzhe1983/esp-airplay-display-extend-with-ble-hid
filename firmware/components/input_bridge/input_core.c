/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "input_core.h"
#include <stdlib.h>
#include <string.h>

/* Four independent report-mode collections. No Apple vendor identity. */
const uint8_t input_report_descriptor[]={
  0x05,1,0x09,6,0xA1,1,0x85,1,
  0x05,7,0x19,0xE0,0x29,0xE7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,
  0x75,8,0x95,1,0x81,1,0x19,0,0x29,0x65,0x15,0,0x25,0x65,0x75,8,0x95,6,0x81,0,
  0x05,8,0x19,1,0x29,5,0x15,0,0x25,1,0x75,1,0x95,5,0x91,2,0x75,3,0x95,1,0x91,1,0xC0,
  0x05,1,0x09,2,0xA1,1,0x85,2,0x09,1,0xA1,0,
  0x05,9,0x19,1,0x29,3,0x15,0,0x25,1,0x75,1,0x95,3,0x81,2,
  0x75,5,0x95,1,0x81,1,0x05,1,0x09,0x30,0x09,0x31,0x09,0x38,
  0x15,0x81,0x25,0x7F,0x75,8,0x95,3,0x81,6,0xC0,0xC0,
  0x05,1,0x09,2,0xA1,1,0x85,3,0x09,1,0xA1,0,
  0x05,9,0x19,1,0x29,3,0x15,0,0x25,1,0x75,1,0x95,3,0x81,2,
  0x75,5,0x95,1,0x81,1,0x05,1,0x09,0x30,0x09,0x31,
  0x15,0,0x26,0xFF,0x7F,0x75,16,0x95,2,0x81,2,0xC0,0xC0,
  0x05,0x0C,0x09,1,0xA1,1,0x85,4,0x15,0,0x26,0xFF,3,
  0x19,0,0x2A,0xFF,3,0x75,16,0x95,1,0x81,0,0xC0
};
const size_t input_report_descriptor_size=sizeof input_report_descriptor;

size_t input_report_size(uint8_t report) {
    static const uint8_t sizes[] = {8, 4, 5, 2};
    return report >= INPUT_KEYBOARD && report <= INPUT_CONSUMER ? sizes[report - 1] : 0;
}

bool input_make_release(uint8_t report, const uint8_t *previous, size_t previous_len,
                        uint8_t *out, size_t capacity) {
    size_t len = input_report_size(report);
    if (!len || !out || capacity < len || (previous && previous_len != len)) return false;
    memset(out, 0, len);
    if (report == INPUT_ABSOLUTE && previous) {
        memcpy(out + 1, previous + 1, 4);
        out[2] &= 0x7f;
        out[4] &= 0x7f;
    }
    return true;
}

static int clamp(int value, int minimum, int maximum) {
    return value < minimum ? minimum : value > maximum ? maximum : value;
}

static bool emit(input_engine_t *engine, uint8_t report, const uint8_t *data, size_t len) {
    if (!engine || !engine->emit || !engine->emit(report, data, len, engine->ctx)) {
        if (engine) engine->suppressed = true;
        return false;
    }
    return true;
}

static bool mouse(input_engine_t *engine, uint8_t buttons, int dx, int dy, int wheel) {
    do {
        int x = clamp(dx, -127, 127), y = clamp(dy, -127, 127), z = clamp(wheel, -127, 127);
        uint8_t report[] = {buttons, (uint8_t)x, (uint8_t)y, (uint8_t)z};
        if (!emit(engine, INPUT_MOUSE, report, sizeof report)) return false;
        dx -= x;
        dy -= y;
        wheel -= z;
    } while (dx || dy || wheel);
    return true;
}

bool input_key(input_engine_t *engine, uint8_t modifiers, uint8_t usage, bool down) {
    if (!engine || !engine->emit || usage > 0x65) return false;
    uint8_t report[8] = {0};
    if (down) {
        report[0] = modifiers;
        report[2] = usage;
    }
    /* This API represents one chord: releasing it releases its modifiers too. */
    return emit(engine, INPUT_KEYBOARD, report, sizeof report);
}

void input_engine_init(input_engine_t *engine, input_emit_fn callback, void *ctx) {
    if (!engine) return;
    memset(engine, 0, sizeof *engine);
    engine->emit = callback;
    engine->ctx = ctx;
    engine->map = (input_mapping_t){.w = 1, .h = 1, .xmax = 32767, .ymax = 32767};
}

bool input_map_point(const input_mapping_t *map, int x, int y, uint16_t *hx, uint16_t *hy) {
    if (!map || !hx || !hy || map->w < 2 || map->h < 2 ||
        map->xmin > map->xmax || map->ymin > map->ymax || map->xmax > 32767 || map->ymax > 32767) return false;
    int64_t rx = (int64_t)x - map->x, ry = (int64_t)y - map->y;
    if (rx < 0 || ry < 0 || rx >= map->w || ry >= map->h) return false;
    *hx = (uint16_t)(map->xmin + (uint64_t)rx * (map->xmax - map->xmin) / (uint32_t)(map->w - 1));
    *hy = (uint16_t)(map->ymin + (uint64_t)ry * (map->ymax - map->ymin) / (uint32_t)(map->h - 1));
    return true;
}

static bool absolute_mouse(input_engine_t *engine, uint8_t buttons, int x, int y) {
    uint16_t hx, hy;
    if (!input_map_point(&engine->map, x, y, &hx, &hy)) return false;
    uint8_t report[] = {buttons, (uint8_t)hx, (uint8_t)(hx >> 8), (uint8_t)hy, (uint8_t)(hy >> 8)};
    if (!emit(engine, INPUT_ABSOLUTE, report, sizeof report)) return false;
    engine->last_hx = hx;
    engine->last_hy = hy;
    engine->have_absolute = true;
    return true;
}

static void clear_gesture(input_engine_t *engine) {
    engine->active = false;
    engine->drag = false;
    engine->moved = false;
    engine->multitouch = false;
    engine->last_count = 0;
    engine->scroll_remainder = 0;
}

static void release_absolute(input_engine_t *engine) {
    if (!engine->have_absolute) return;
    uint8_t report[] = {0, (uint8_t)engine->last_hx, (uint8_t)(engine->last_hx >> 8),
                       (uint8_t)engine->last_hy, (uint8_t)(engine->last_hy >> 8)};
    (void)emit(engine, INPUT_ABSOLUTE, report, sizeof report);
}

void input_engine_cancel(input_engine_t *engine) {
    if (!engine || !engine->emit) return;
    /* Release all potentially latched state, even when a previous send failed. */
    (void)mouse(engine, 0, 0, 0, 0);
    release_absolute(engine);
    (void)input_key(engine, 0, 0, false);
    uint8_t consumer[2] = {0};
    (void)emit(engine, INPUT_CONSUMER, consumer, sizeof consumer);
    clear_gesture(engine);
    engine->suppressed = false;
}

void input_engine_update(input_engine_t *engine, const input_point_t *points, size_t count, uint32_t now) {
    if (!engine || !engine->emit) return;
    if ((count && !points) || count > 2 || (engine->absolute && count > 1)) {
        if (!engine->suppressed) input_engine_cancel(engine);
        engine->suppressed = true;
        return;
    }
    if (!count) {
        if (engine->active) {
            if (engine->absolute) {
                release_absolute(engine);
            } else {
                if (!engine->suppressed && !engine->moved && !engine->drag && now - engine->started_ms < 350)
                    (void)mouse(engine, engine->multitouch ? 2 : 1, 0, 0, 0);
                (void)mouse(engine, 0, 0, 0, 0);
            }
        }
        clear_gesture(engine);
        engine->suppressed = false;
        return;
    }
    /* Transport failure or unsupported contact count: wait for every finger to lift. */
    if (engine->suppressed) return;
    int x = points[0].x, y = points[0].y;
    if (count == 2) {
        x = (x + points[1].x) / 2;
        y = (y + points[1].y) / 2;
    }
    if (!engine->active) {
        if (engine->absolute) {
            uint16_t hx, hy;
            if (!input_map_point(&engine->map, x, y, &hx, &hy)) return;
        }
        engine->active = true;
        engine->start_x = engine->last_x = x;
        engine->start_y = engine->last_y = y;
        engine->started_ms = now;
        engine->last_count = (uint8_t)count;
        engine->multitouch = count > 1;
        if (engine->absolute) (void)absolute_mouse(engine, 1, x, y);
        return;
    }
    if (engine->absolute) {
        if (absolute_mouse(engine, 1, x, y)) {
            engine->last_x = x;
            engine->last_y = y;
        }
        return;
    }
    if (count != engine->last_count) {
        if (engine->drag) {
            (void)mouse(engine, 0, 0, 0, 0);
            engine->moved = true; /* Adding a finger must not turn a drag into a right click. */
        }
        if (!engine->multitouch && count == 2) engine->started_ms = now;
        engine->drag = false;
        engine->start_x = engine->last_x = x;
        engine->start_y = engine->last_y = y;
        engine->last_count = (uint8_t)count;
        engine->multitouch = true;
        engine->scroll_remainder = 0;
        return;
    }
    int dx = x - engine->last_x, dy = y - engine->last_y;
    if (count == 1 && !engine->multitouch && !engine->moved && !engine->drag && now - engine->started_ms >= 450) {
        engine->drag = true;
        (void)mouse(engine, 1, 0, 0, 0);
    }
    if (abs(x - engine->start_x) + abs(y - engine->start_y) > 6) engine->moved = true;
    if (count == 2) {
        engine->multitouch = true;
        engine->scroll_remainder -= dy;
        int wheel = engine->scroll_remainder / 12;
        if (wheel) {
            (void)mouse(engine, 0, 0, 0, wheel);
            engine->scroll_remainder -= wheel * 12;
            engine->moved = true;
        }
    } else if (!engine->multitouch && (dx || dy) && !engine->suppressed) {
        (void)mouse(engine, engine->drag ? 1 : 0, dx, dy, 0);
    }
    engine->last_x = x;
    engine->last_y = y;
}

bool input_ascii(char c, uint8_t *usage, uint8_t *modifiers) {
    if (!usage || !modifiers) return false;
    *usage = 0;
    *modifiers = 0;
    if (c >= 'a' && c <= 'z') { *usage = (uint8_t)(4 + c - 'a'); return true; }
    if (c >= 'A' && c <= 'Z') { *usage = (uint8_t)(4 + c - 'A'); *modifiers = MOD_SHIFT; return true; }
    if (c >= '1' && c <= '9') { *usage = (uint8_t)(0x1e + c - '1'); return true; }
    if (c == '0') { *usage = 0x27; return true; }
    if (c == '\r') { *usage = 0x28; return true; }
    if (c == '\x7f') { *usage = 0x4c; return true; }
    static const char plain[] = "\n\x1b\b\t -=[]\\;'`,./";
    static const uint8_t keys[] = {0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,0x30,0x31,0x33,0x34,0x35,0x36,0x37,0x38};
    _Static_assert(sizeof plain - 1 == sizeof keys, "ASCII key table length");
    for (size_t i = 0; i < sizeof keys; ++i)
        if (c == plain[i]) { *usage = keys[i]; return true; }
    static const char shifted[] = "!@#$%^&*()_+{}|:\"~<>?";
    static const uint8_t shift_keys[] = {0x1e,0x1f,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x2d,0x2e,0x2f,0x30,0x31,0x33,0x34,0x35,0x36,0x37,0x38};
    _Static_assert(sizeof shifted - 1 == sizeof shift_keys, "Shift key table length");
    for (size_t i = 0; i < sizeof shift_keys; ++i)
        if (c == shifted[i]) { *usage = shift_keys[i]; *modifiers = MOD_SHIFT; return true; }
    return false;
}
