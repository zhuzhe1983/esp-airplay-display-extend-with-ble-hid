/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AP_CODEC_MAX 4096u
#define AP_MAX_PAYLOAD (2u * 1024u * 1024u)

bool avcc_to_annexb(uint8_t *data, size_t len);
bool annexb_has_idr(const uint8_t *data, size_t len);
/* avcC, 4-byte NAL lengths only. Returns 0 for malformed/unsupported records. */
size_t ap_avcc_config(const uint8_t *data, size_t len, uint8_t *out, size_t capacity);

