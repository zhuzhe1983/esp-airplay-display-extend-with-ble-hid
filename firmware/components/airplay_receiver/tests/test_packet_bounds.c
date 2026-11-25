/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host tests for network-controlled lengths. They do not validate AirPlay interoperability.
 */
#include "h264_packet.h"
#include "bplist.h"
#include "display_edid.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void h264_lengths(void) {
    uint8_t au[] = {0, 0, 0, 2, 0x65, 0x80, 0, 0, 0, 2, 0x61, 0x80};
    assert(avcc_to_annexb(au, sizeof au));
    assert(annexb_has_idr(au, sizeof au));
    uint8_t invalid[] = {0, 0, 0, 2, 0x65, 0x80, 0xff, 0xff, 0xff, 0xff, 0x61};
    uint8_t saved[sizeof invalid];
    memcpy(saved, invalid, sizeof saved);
    assert(!avcc_to_annexb(invalid, sizeof invalid));
    assert(!memcmp(saved, invalid, sizeof saved)); /* Never partially rewrite a malformed packet. */
    uint8_t empty[] = {0, 0, 0, 0};
    assert(!avcc_to_annexb(empty, sizeof empty));
    assert(!avcc_to_annexb(NULL, 0));

    const uint8_t avcc[] = {1, 100, 0, 31, 0xff, 0xe1, 0, 4, 0x67, 100, 0, 31, 1, 0, 2, 0x68, 0xee};
    uint8_t config[AP_CODEC_MAX];
    assert(ap_avcc_config(avcc, sizeof avcc, config, sizeof config) == 14);
    assert(config[3] == 1 && config[4] == 0x67 && config[11] == 1 && config[12] == 0x68);
    for (size_t n = 0; n < sizeof avcc; ++n) assert(ap_avcc_config(avcc, n, config, sizeof config) == 0);
    assert(ap_avcc_config(avcc, sizeof avcc, config, 13) == 0);
}

static void plist_roundtrip(void) {
    bp_writer w;
    bp_writer_init(&w);
    int root = bp_w_dict(&w), streams = bp_w_array(&w), stream = bp_w_dict(&w);
    bp_w_set(&w, root, "streams", streams);
    bp_w_append(&w, streams, stream);
    bp_w_set(&w, stream, "type", bp_w_uint(&w, 110));
    bp_w_set(&w, stream, "streamConnectionID", bp_w_uint(&w, UINT64_MAX));
    bp_w_set(&w, root, "timingProtocol", bp_w_string(&w, "NTP"));
    size_t len = 0;
    uint8_t *data = bp_w_finish(&w, &len);
    bp_writer_free(&w);
    assert(data && len);
    bp_reader r;
    int top = bp_open(&r, data, len);
    assert(top >= 0 && bp_type(&r, top) == BP_DICT);
    int items = bp_dict_get(&r, top, "streams");
    assert(bp_count(&r, items) == 1);
    uint64_t id = 0;
    assert(bp_get_uint(&r, bp_dict_get(&r, bp_array_get(&r, items, 0), "streamConnectionID"), &id));
    assert(id == UINT64_MAX);
    /* A wrapping offset table used to pass addition-based bounds checks. */
    memset(data + len - 8, 0xff, 8);
    assert(bp_open(&r, data, len) == -1);
    assert(bp_type(&r, 0) == BP_NONE);
    free(data);
}

static uint32_t random_state = 0x2468ace1;
static uint32_t next_random(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void malformed_inputs(void) {
    uint8_t data[512], output[1024];
    for (unsigned iteration = 0; iteration < 20000; ++iteration) {
        size_t len = next_random() % sizeof data;
        for (size_t n = 0; n < len; ++n) data[n] = (uint8_t)next_random();
        if (len >= 40 && (iteration & 1)) memcpy(data, "bplist00", 8);
        bp_reader r;
        int top = bp_open(&r, data, len);
        int ref = bp_dict_get(&r, top, "streams");
        uint64_t integer;
        double real;
        bool boolean;
        const uint8_t *bytes;
        size_t byte_len;
        char string[64];
        (void)bp_count(&r, ref);
        (void)bp_array_get(&r, ref, 0);
        (void)bp_get_uint(&r, ref, &integer);
        (void)bp_get_real(&r, ref, &real);
        (void)bp_get_bool(&r, ref, &boolean);
        (void)bp_get_data(&r, ref, &bytes, &byte_len);
        (void)bp_get_string(&r, ref, string, sizeof string);
        (void)ap_avcc_config(data, len, output, sizeof output);
        (void)avcc_to_annexb(data, len);
    }
}

static void display_description(void) {
    uint8_t e[128];ap_display_edid(e,720,720,71,71,30);
    unsigned sum=0;for(unsigned i=0;i<128;i++)sum+=e[i];
    assert((sum&255)==0&&e[18]==1&&e[19]==4&&e[126]==0);
    const uint8_t *d=e+54;
    unsigned w=d[2]+((d[4]&0xf0)<<4),h=d[5]+((d[7]&0xf0)<<4);
    unsigned hb=d[3]+((d[4]&15)<<8),vb=d[6]+((d[7]&15)<<8);
    unsigned clock=(d[0]+(d[1]<<8))*10000;
    assert(w==720&&h==720&&clock==(w+hb)*(h+vb)*30);
    assert(d[12]+((d[14]&0xf0)<<4)==71&&d[13]+((d[14]&15)<<8)==71);
    assert((e[24]&2)&&e[75]==0xfc&&e[93]==0xfd);
    for(unsigned i=35;i<38;i++)assert(e[i]==0);
    for(unsigned i=38;i<54;i++)assert(e[i]==1);
}

int main(void) {
    display_description();
    h264_lengths();
    plist_roundtrip();
    malformed_inputs();
    puts("packet bounds: H.264 framing, plist roundtrip/overflow, 20,000 malformed inputs passed");
    return 0;
}
