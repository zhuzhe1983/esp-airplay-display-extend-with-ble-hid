/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { INPUT_KEYBOARD=1, INPUT_MOUSE=2, INPUT_ABSOLUTE=3, INPUT_CONSUMER=4 };
enum { MOD_CTRL=1, MOD_SHIFT=2, MOD_ALT=4, MOD_GUI=8 };
typedef bool (*input_emit_fn)(uint8_t report, const uint8_t *data, size_t len, void *ctx);
typedef struct { uint16_t x,y; } input_point_t;
typedef struct { int x,y,w,h; uint16_t xmin,ymin,xmax,ymax; } input_mapping_t;
typedef struct {
    input_emit_fn emit; void *ctx;
    bool active,moved,drag,multitouch,absolute,suppressed,have_absolute;
    uint8_t last_count;
    int start_x,start_y,last_x,last_y,scroll_remainder;
    uint32_t started_ms;
    uint16_t last_hx,last_hy;
    input_mapping_t map;
} input_engine_t;
extern const uint8_t input_report_descriptor[];
extern const size_t input_report_descriptor_size;
/* Report bodies exclude the report ID on BLE HOGP's report characteristic. */
size_t input_report_size(uint8_t report);
bool input_make_release(uint8_t report,const uint8_t *previous,size_t previous_len,uint8_t *out,size_t capacity);
void input_engine_init(input_engine_t *,input_emit_fn,void *ctx);
void input_engine_cancel(input_engine_t *);
void input_engine_update(input_engine_t *,const input_point_t *,size_t count,uint32_t now_ms);
/* One chord at a time. down=false releases all keys and all chord modifiers. */
bool input_key(input_engine_t *,uint8_t modifiers,uint8_t usage,bool down);
/* US ANSI physical key mapping; host input-source layout determines the resulting character. */
bool input_ascii(char c,uint8_t *usage,uint8_t *modifiers);
bool input_map_point(const input_mapping_t *,int x,int y,uint16_t *hx,uint16_t *hy);
#ifdef __cplusplus
}
#endif
