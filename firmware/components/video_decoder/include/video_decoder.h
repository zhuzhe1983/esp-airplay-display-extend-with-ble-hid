#ifndef P4_VIDEO_DECODER_H
#define P4_VIDEO_DECODER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct video_decoder video_decoder_t;

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

/* Called synchronously on the caller's decode task. Copy if retained or queued.
 * rgb565 is valid until the next push/flush/reset/destroy. Do not reenter. */
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

#ifdef __cplusplus
}
#endif
#endif
