/* SPDX-License-Identifier: GPL-3.0-or-later
 * AVCC/Annex B helpers derived from esp32-iphone-agent's MIT-licensed video/gop.c.
 * Copyright (c) 2026 Henry Yuan. See LICENSE-UPSTREAM-MIT.
 * This port validates the entire packet before rewriting lengths in place.
 */
#include "h264_packet.h"
#include <string.h>

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

bool avcc_to_annexb(uint8_t *p, size_t len) {
    if (!p || len < 5 || len > AP_MAX_PAYLOAD) return false;
    size_t at = 0;
    while (at < len) {
        if (len - at < 5) return false;
        uint32_t n = be32(p + at);
        if (!n || n > len - at - 4 || (p[at + 4] & 0x80)) return false;
        unsigned type = p[at + 4] & 0x1f;
        if (!type || type > 23) return false;
        at += 4 + n;
    }
    for (at = 0; at < len;) {
        uint32_t n = be32(p + at);
        p[at] = p[at + 1] = p[at + 2] = 0;
        p[at + 3] = 1;
        at += 4 + n;
    }
    return true;
}

bool annexb_has_idr(const uint8_t *p, size_t len) {
    if (!p) return false;
    for (size_t i = 0; i + 3 < len; ++i)
        if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1 && (p[i + 3] & 0x1f) == 5)
            return true;
    return false;
}

static bool config_group(const uint8_t *p, size_t len, size_t *at, unsigned count,
                         unsigned want_type, uint8_t *out, size_t cap, size_t *written) {
    for (unsigned i = 0; i < count; ++i) {
        if (len - *at < 2) return false;
        size_t n = (size_t)p[*at] << 8 | p[*at + 1];
        *at += 2;
        if (!n || n > len - *at || cap - *written < 4 || n > cap - *written - 4 ||
            (p[*at] & 0x80) || (p[*at] & 0x1f) != want_type) return false;
        static const uint8_t sc[] = {0, 0, 0, 1};
        memcpy(out + *written, sc, sizeof sc);
        memcpy(out + *written + 4, p + *at, n);
        *written += 4 + n;
        *at += n;
    }
    return true;
}

size_t ap_avcc_config(const uint8_t *p, size_t len, uint8_t *out, size_t cap) {
    if (!p || !out || len < 7 || p[0] != 1 || (p[4] & 3) != 3) return 0;
    unsigned sps = p[5] & 0x1f;
    size_t at = 6, written = 0;
    if (!sps || !config_group(p, len, &at, sps, 7, out, cap, &written) || at >= len) return 0;
    unsigned pps = p[at++];
    if (!pps || !config_group(p, len, &at, pps, 8, out, cap, &written)) return 0;
    /* High-profile extension fields may follow; SPS/PPS already contain their decoder parameters. */
    return written;
}
