/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
typedef struct {
    uint32_t decoded_outputs, replaced_outputs, frame_age_ms;
} video_pipeline_stats_t;
/* Atomic diagnostics. Age is decoded snapshot -> UI, not network end-to-end. */
void video_pipeline_get_stats(video_pipeline_stats_t *out);
