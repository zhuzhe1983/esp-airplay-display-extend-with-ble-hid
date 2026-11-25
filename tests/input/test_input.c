/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "input_core.h"
#include "input_tx_queue.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t id, data[8]; size_t len; } captured_t;
static captured_t captured[4096];
static size_t captured_count, attempts, fail_after;

static bool capture(uint8_t id, const uint8_t *data, size_t len, void *ctx) {
    (void)ctx;
    ++attempts;
    if (captured_count >= fail_after) return false;
    assert(id >= 1 && id <= 4 && len == input_report_size(id));
    assert(captured_count < sizeof captured / sizeof captured[0]);
    captured[captured_count].id = id;
    captured[captured_count].len = len;
    memcpy(captured[captured_count++].data, data, len);
    return true;
}

static input_engine_t engine(void) {
    input_engine_t value;
    captured_count = attempts = 0;
    fail_after = SIZE_MAX;
    input_engine_init(&value, capture, NULL);
    return value;
}

static void descriptor(void) {
    unsigned input_bits[5] = {0}, output_bits[5] = {0};
    unsigned report = 0, size = 0, count = 0, collections = 0, ids = 0;
    int32_t logical_min = 0, logical_max = 0;
    assert(input_report_descriptor_size > 100 && input_report_descriptor_size <= 512);
    for (size_t at = 0; at < input_report_descriptor_size;) {
        uint8_t prefix = input_report_descriptor[at++];
        assert(prefix != 0xfe);
        unsigned bytes = prefix & 3;
        if (bytes == 3) bytes = 4;
        assert(bytes <= input_report_descriptor_size - at);
        uint32_t value = 0;
        for (unsigned j = 0; j < bytes; ++j) value |= (uint32_t)input_report_descriptor[at++] << (8 * j);
        unsigned type = (prefix >> 2) & 3, tag = prefix >> 4;
        if (type == 1) {
            if (tag == 7) size = value;
            else if (tag == 8) { report = value; assert(report >= 1 && report <= 4); ids |= 1u << (report - 1); }
            else if (tag == 9) count = value;
            else if (tag == 1) logical_min = bytes == 1 ? (int8_t)value : bytes == 2 ? (int16_t)value : (int32_t)value;
            else if (tag == 2) logical_max = logical_min < 0 ?
                (bytes == 1 ? (int8_t)value : bytes == 2 ? (int16_t)value : (int32_t)value) : (int32_t)value;
        } else if (type == 0 && (tag == 8 || tag == 9)) {
            assert(report >= 1 && report <= 4 && size > 0 && size <= 16 && count > 0);
            if (!(value & 1)) {
                assert(logical_min <= logical_max);
                assert(logical_max <= (logical_min < 0 ? (1 << (size - 1)) - 1 : (1 << size) - 1));
            }
            if (tag == 8) input_bits[report] += size * count;
            else output_bits[report] += size * count;
        } else if (type == 0 && tag == 10) ++collections;
        else if (type == 0 && tag == 12) { assert(collections); --collections; }
    }
    assert(!collections && ids == 15);
    for (uint8_t id = 1; id <= 4; ++id) assert(input_bits[id] == input_report_size(id) * 8);
    assert(output_bits[1] == 8 && !output_bits[2] && !output_bits[3] && !output_bits[4]);
    assert(input_report_size(0) == 0 && input_report_size(5) == 0);
}

static void taps_and_scroll(void) {
    input_engine_t e = engine();
    input_point_t p[3] = {{100, 100}, {120, 100}, {140, 100}};
    input_engine_update(&e, p, 1, 10);
    input_engine_update(&e, NULL, 0, 100);
    assert(captured_count == 2 && captured[0].data[0] == 1 && captured[1].data[0] == 0);
    e = engine();
    input_engine_update(&e, p, 2, 0);
    input_engine_update(&e, p, 1, 50); /* Fingers need not lift in precisely the same sample. */
    input_engine_update(&e, NULL, 0, 100);
    assert(captured_count == 2 && captured[0].data[0] == 2 && captured[1].data[0] == 0);
    e = engine();
    input_engine_update(&e, p, 2, 0);
    p[0].y = p[1].y = 124;
    input_engine_update(&e, p, 2, 20);
    input_engine_update(&e, NULL, 0, 40);
    assert(captured_count == 2 && captured[0].id == INPUT_MOUSE);
    assert(captured[0].data[0] == 0 && captured[0].data[1] == 0 && captured[0].data[2] == 0);
    assert((int8_t)captured[0].data[3] == -2 && captured[1].data[0] == 0);
    e = engine();
    input_engine_update(&e, p, 3, 0);
    size_t releases = captured_count;
    input_engine_update(&e, p, 3, 20);
    input_engine_update(&e, p, 1, 40);
    assert(captured_count == releases && e.suppressed);
    input_engine_update(&e, NULL, 0, 60);
    assert(!e.suppressed);
    for (size_t i = 0; i < captured_count; ++i) assert(captured[i].data[0] == 0);
}

static void dragging_and_large_motion(void) {
    input_engine_t e = engine();
    input_point_t p = {100, 100};
    input_engine_update(&e, &p, 1, 0);
    p.x = 130; p.y = 120;
    input_engine_update(&e, &p, 1, 500);
    assert(e.drag && captured_count == 2);
    assert(captured[0].data[0] == 1 && captured[1].data[0] == 1);
    assert((int8_t)captured[1].data[1] == 30 && (int8_t)captured[1].data[2] == 20);
    input_engine_update(&e, NULL, 0, 550);
    assert(captured_count == 3 && captured[2].data[0] == 0);
    e = engine();
    p = (input_point_t){0, 0};
    input_engine_update(&e, &p, 1, 0);
    p = (input_point_t){65535, 65535};
    input_engine_update(&e, &p, 1, 20);
    int sum_x = 0, sum_y = 0;
    for (size_t i = 0; i < captured_count; ++i) {
        assert(captured[i].id == INPUT_MOUSE && captured[i].data[0] == 0);
        sum_x += (int8_t)captured[i].data[1];
        sum_y += (int8_t)captured[i].data[2];
        assert((int8_t)captured[i].data[1] >= -127 && (int8_t)captured[i].data[2] >= -127);
    }
    assert(sum_x == 65535 && sum_y == 65535 && captured_count == 517);
    p = (input_point_t){0, 0};
    input_engine_update(&e, &p, 1, 30);
    for (size_t i = 517; i < captured_count; ++i) {
        sum_x += (int8_t)captured[i].data[1];
        sum_y += (int8_t)captured[i].data[2];
    }
    assert(sum_x == 0 && sum_y == 0);
    e = engine();
    input_engine_update(&e, &p, 1, UINT32_MAX - 400u);
    input_engine_update(&e, &p, 1, 200);
    assert(e.drag); /* Millisecond clock wrap does not break long press. */
    input_engine_cancel(&e);
    assert(!e.active && !e.drag && captured[1].data[0] == 0);
}

static void absolute_mapping(void) {
    input_mapping_t m = {.x = 10, .y = 20, .w = 720, .h = 1280, .xmin = 100, .ymin = 200, .xmax = 30000, .ymax = 32767};
    uint16_t x, y;
    assert(input_map_point(&m, 10, 20, &x, &y) && x == 100 && y == 200);
    assert(input_map_point(&m, 729, 1299, &x, &y) && x == 30000 && y == 32767);
    assert(!input_map_point(&m, 9, 20, &x, &y));
    assert(!input_map_point(&m, 730, 1299, &x, &y));
    m.x = m.y = INT_MIN; m.w = m.h = INT_MAX; m.xmin = m.ymin = 0; m.xmax = m.ymax = 32767;
    assert(input_map_point(&m, -2, -2, &x, &y) && x == 32767 && y == 32767);
    assert(!input_map_point(&m, INT_MAX, INT_MAX, &x, &y));
    input_engine_t e = engine();
    e.absolute = true;
    e.map = (input_mapping_t){.w = 720, .h = 720, .xmax = 32767, .ymax = 32767};
    input_point_t p = {719, 719};
    input_engine_update(&e, &p, 1, 0);
    assert(captured_count == 1 && captured[0].id == INPUT_ABSOLUTE && captured[0].data[0] == 1);
    assert(captured[0].data[1] == 0xff && captured[0].data[2] == 0x7f);
    e.map.w = 0; /* A layout change must not prevent releasing the previous button. */
    input_engine_update(&e, NULL, 0, 20);
    assert(captured_count == 2 && captured[1].data[0] == 0);
    assert(!memcmp(captured[0].data + 1, captured[1].data + 1, 4));
}

static void keyboard_mapping(void) {
    uint8_t usage, modifiers;
    for (int c = 32; c <= 126; ++c) assert(input_ascii((char)c, &usage, &modifiers));
    assert(input_ascii('K', &usage, &modifiers) && usage == 0x0e && modifiers == MOD_SHIFT);
    assert(input_ascii('~', &usage, &modifiers) && usage == 0x35 && modifiers == MOD_SHIFT);
    assert(input_ascii('\\', &usage, &modifiers) && usage == 0x31 && modifiers == 0);
    assert(input_ascii('\n', &usage, &modifiers) && usage == 0x28);
    assert(input_ascii('\x7f', &usage, &modifiers) && usage == 0x4c);
    assert(!input_ascii('\0', &usage, &modifiers) && usage == 0 && modifiers == 0);
    assert(!input_ascii('a', NULL, &modifiers));
    input_engine_t e = engine();
    assert(input_key(&e, MOD_GUI | MOD_SHIFT, 0x0e, true));
    assert(captured[0].data[0] == 0x0a && captured[0].data[2] == 0x0e && captured[0].len == 8);
    assert(input_key(&e, MOD_GUI | MOD_SHIFT, 0x0e, false));
    const uint8_t zero[8] = {0};
    assert(!memcmp(captured[1].data, zero, sizeof zero));
}

static void queue_overflow_and_release(void) {
    input_tx_queue_t q;
    input_tx_init(&q);
    uint8_t mouse[4] = {1, 10, 20, 0};
    for (size_t i = 0; i < INPUT_TX_CAPACITY; ++i) {
        mouse[1] = (uint8_t)i;
        assert(input_tx_push(&q, INPUT_MOUSE, mouse, sizeof mouse));
    }
    uint32_t old_generation = q.generation;
    assert(!input_tx_push(&q, INPUT_MOUSE, mouse, sizeof mouse));
    assert(q.resetting && !q.count && q.generation != old_generation);
    input_tx_report_t report;
    assert(!input_tx_pop(&q, &report));
    assert(!input_tx_push(&q, INPUT_MOUSE, mouse, sizeof mouse));
    assert(!input_tx_finish_reset(&q, old_generation));
    uint8_t previous[8] = {0xff, 0x34, 0x12, 0x78, 0x56, 0xff, 0xff, 0xff}, release[8];
    for (uint8_t id = 1; id <= 4; ++id) {
        assert(input_make_release(id, previous, input_report_size(id), release, sizeof release));
        assert(release[0] == 0);
        if (id == INPUT_ABSOLUTE) assert(!memcmp(release + 1, previous + 1, 4));
        else for (size_t i = 0; i < input_report_size(id); ++i) assert(release[i] == 0);
    }
    assert(input_tx_finish_reset(&q, q.generation));
    for (size_t i = 0; i < INPUT_TX_CAPACITY; ++i) {
        mouse[1] = (uint8_t)i;
        assert(input_tx_push(&q, INPUT_MOUSE, mouse, sizeof mouse));
    }
    for (size_t i = 0; i < INPUT_TX_CAPACITY; ++i) {
        assert(input_tx_pop(&q, &report));
        assert(report.data[1] == i && report.generation == q.generation);
    }
    assert(!input_tx_pop(&q, &report));
    assert(!input_tx_push(&q, INPUT_MOUSE, mouse, 3));
    input_engine_t e = engine();
    fail_after = 2;
    input_point_t p = {0, 0};
    input_engine_update(&e, &p, 1, 0);
    p.x = 5000;
    input_engine_update(&e, &p, 1, 10);
    assert(captured_count == 2 && attempts == 3 && e.suppressed);
    p.x = 6000;
    input_engine_update(&e, &p, 1, 20);
    assert(attempts == 3); /* Stop generating motion immediately when transport refuses it. */
    input_engine_update(&e, NULL, 0, 30);
    assert(!e.suppressed && !e.active);
}

int main(void) {
    descriptor();
    taps_and_scroll();
    dragging_and_large_motion();
    absolute_mapping();
    keyboard_mapping();
    queue_overflow_and_release();
    puts("input: descriptor/report sizes, taps/right-click/scroll/drag, large motion, absolute bounds, ASCII/chords, queue overflow/release passed");
    return 0;
}
