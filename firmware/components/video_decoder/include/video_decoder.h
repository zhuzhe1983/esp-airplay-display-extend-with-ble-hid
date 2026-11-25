#ifndef P4_VIDEO_DECODER_H
#define P4_VIDEO_DECODER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct video_decoder video_decoder_t;
typedef struct video_converter video_converter_t;

/* Borrowed I420 image in display order. Copy planes before returning if retained.
 * Strides are in bytes. All metadata belongs to this image, not the next AU. */
typedef struct {
    const uint8_t *planes[3];
    size_t strides[3];
    uint16_t width, height, sar_width, sar_height;
    bool full_range, bt709;
    uint64_t frame_number;
} video_decoder_yuv_frame_t;
typedef void (*video_decoder_yuv_cb_t)(void *user, const video_decoder_yuv_frame_t *frame);

typedef struct {
    uint16_t x, y, width, height;
} video_decoder_rect_t;

typedef struct {
    /* Native-endian RGB565 values; the panel driver owns any bus-byte swap. */
    const uint16_t *rgb565;
    uint16_t width, height;
    size_t stride_bytes;
    video_decoder_rect_t view_rect;
    uint16_t source_width, source_height;
    uint64_t frame_number;
} video_decoder_frame_t;

/* Called synchronously by decoder push/flush or converter render. Copy if retained.
 * rgb565 lasts until the owning handle's next operation. Do not reenter it. */
typedef void (*video_decoder_frame_cb_t)(void *user,
                                         const video_decoder_frame_t *frame);

typedef struct {
    uint16_t panel_width, panel_height;
    /* Zero selects 1280. Each dimension <=1920; visible pixels <=2073600.
     * Coded dimensions may include the required 16-pixel macroblock padding. */
    uint16_t max_input_width, max_input_height;
    /* Zero selects 1 MiB. Hard limit 4 MiB. One complete Annex-B access unit/call. */
    size_t max_access_unit_bytes;
    video_decoder_frame_cb_t callback;
    void *user;
    /* Optional output policy, after AVC decoding and before RGB conversion.
     * false skips presentation only; reference pictures remain intact. */
    bool (*should_present)(void *user);
    /* Select either callback (synchronous RGB) or yuv_callback (no RGB work).
     * A separate converter can consume owned YUV snapshots on another task. */
    video_decoder_yuv_cb_t yuv_callback;
} video_decoder_config_t;

typedef enum {
    VIDEO_DECODER_OK = 0,
    VIDEO_DECODER_ERR_ARGUMENT = -1,
    VIDEO_DECODER_ERR_MEMORY = -2,
    VIDEO_DECODER_ERR_FORMAT = -3,
    VIDEO_DECODER_ERR_LIMIT = -4,
    VIDEO_DECODER_ERR_CODEC = -5,
    VIDEO_DECODER_ERR_NEEDS_SPS = -6
} video_decoder_result_t;

/* Progressive 8-bit 4:2:0 AVC Baseline/Main/High; software OpenH264 decoding.
 * All calls on a handle must be serialized by its owner. ESP requires PSRAM. */
int video_decoder_init(const video_decoder_config_t *config,
                       video_decoder_t **out_decoder);
/* SPS/PPS may arrive in a parameter-only call; video calls must contain complete
 * access units. Never discard P/B units to throttle presentation: they carry
 * reference state. Success can mean accepted with no output frame yet. */
int video_decoder_push(video_decoder_t *decoder,
                       const uint8_t *annexb, size_t length);
/* End of stream only: drain delayed B frames. Reset before a new session. */
int video_decoder_flush(video_decoder_t *decoder);
int video_decoder_reset(video_decoder_t *decoder);
void video_decoder_destroy(video_decoder_t *decoder);
const char *video_decoder_error_string(int result);

/* One serialized owner per converter; independent from the AVC decoder.
 * Rendering is synchronous. The RGB callback buffer lasts until the next render. */
int video_converter_init(uint16_t panel_width, uint16_t panel_height,
                         video_decoder_frame_cb_t callback, void *user,
                         video_converter_t **out_converter);
int video_converter_render(video_converter_t *converter, const video_decoder_yuv_frame_t *frame);
void video_converter_destroy(video_converter_t *converter);

#ifdef __cplusplus
}
#endif
#endif
