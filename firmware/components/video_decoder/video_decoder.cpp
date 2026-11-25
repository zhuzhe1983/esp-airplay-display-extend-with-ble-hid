#include "video_decoder.h"
#include "codec_api.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <new>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

namespace {
constexpr uint32_t kMaxDimension = 1920;
constexpr uint32_t kMaxVisiblePixels = 1920U * 1080U;
constexpr uint32_t kMaxCodedPixels = 1920U * 1088U; // AVC macroblock padding.
constexpr size_t kDefaultAuBytes = 1024U * 1024U;
constexpr size_t kHardAuBytes = 4U * 1024U * 1024U;
constexpr size_t kMaxParameterBytes = 4096;

struct SpsInfo {
    bool valid = false;
    bool full_range = false;
    bool bt709 = false;
    uint16_t width = 0, height = 0;
    uint16_t sar_width = 1, sar_height = 1;
};
struct ParameterSets {
    SpsInfo sps[32]{};
    uint8_t pps_sps[256]{};
    bool pps_valid[256]{};
    uint8_t active_sps = 0;
};

/* A bounded RBSP reader: neither malformed Exp-Golomb codes nor truncated SPS
 * fields can advance past the buffer. Parameter sets are small, never AUs. */
class Bits {
public:
    Bits(const uint8_t *data, size_t bytes) : data_(data), bits_(bytes * 8) {}
    uint32_t read(unsigned n) {
        if (n > 32 || n > bits_ - std::min(pos_, bits_)) {
            ok_ = false;
            return 0;
        }
        uint32_t v = 0;
        while (n--) {
            v = (v << 1) | ((data_[pos_ / 8] >> (7 - pos_ % 8)) & 1);
            ++pos_;
        }
        return v;
    }
    uint32_t ue() {
        unsigned leading = 0;
        while (ok_ && read(1) == 0) {
            if (++leading > 30) { ok_ = false; return 0; }
        }
        if (!ok_) return 0;
        return ((1U << leading) - 1U) + read(leading);
    }
    int32_t se() {
        const uint32_t v = ue();
        return (v & 1) ? static_cast<int32_t>((v + 1U) / 2U)
                       : -static_cast<int32_t>(v / 2U);
    }
    bool ok() const { return ok_; }
private:
    const uint8_t *data_;
    size_t bits_, pos_ = 0;
    bool ok_ = true;
};

bool rbsp_copy(const uint8_t *nal, size_t n, uint8_t *out, size_t &out_n) {
    if (n < 2 || n > kMaxParameterBytes) return false;
    out_n = 0;
    unsigned zeros = 0;
    for (size_t i = 1; i < n; ++i) {
        const uint8_t byte = nal[i];
        if (zeros >= 2 && byte == 3) {
            if (i + 1 == n || nal[i + 1] > 3) return false;
            zeros = 0;
            continue;
        }
        out[out_n++] = byte;
        zeros = byte == 0 ? zeros + 1 : 0;
    }
    return true;
}

bool skip_scaling_list(Bits &bits, unsigned count) {
    int last = 8, next = 8;
    for (unsigned i = 0; i < count && bits.ok(); ++i) {
        if (next) {
            const int delta = bits.se();
            if (delta < -128 || delta > 127) return false;
            next = (last + delta + 256) & 255;
        }
        if (next) last = next;
    }
    return bits.ok();
}

int parse_sps(const uint8_t *nal, size_t size,
              const video_decoder_config_t &config,
              uint8_t &sps_id, SpsInfo &info) {
    uint8_t rbsp[kMaxParameterBytes];
    size_t rbsp_size = 0;
    if (!rbsp_copy(nal, size, rbsp, rbsp_size)) return VIDEO_DECODER_ERR_FORMAT;
    Bits b(rbsp, rbsp_size);
    const uint32_t profile = b.read(8);
    b.read(8); // constraint flags / reserved bits (also validated by OpenH264).
    b.read(8); // level_idc does not replace our own memory bounds.
    const uint32_t id = b.ue();
    if (!b.ok() || id >= 32) return VIDEO_DECODER_ERR_FORMAT;
    if (profile != 66 && profile != 77 && profile != 100)
        return VIDEO_DECODER_ERR_FORMAT;
    if (profile == 100) {
        const uint32_t chroma = b.ue();
        if (chroma != 1) return VIDEO_DECODER_ERR_FORMAT;
        if (b.ue() != 0 || b.ue() != 0) return VIDEO_DECODER_ERR_FORMAT;
        b.read(1); // qpprime_y_zero_transform_bypass_flag
        if (b.read(1)) {
            for (unsigned i = 0; i < 8; ++i)
                if (b.read(1) && !skip_scaling_list(b, i < 6 ? 16 : 64))
                    return VIDEO_DECODER_ERR_FORMAT;
        }
    }
    if (b.ue() > 12) return VIDEO_DECODER_ERR_FORMAT;
    const uint32_t poc_type = b.ue();
    if (poc_type == 0) {
        if (b.ue() > 12) return VIDEO_DECODER_ERR_FORMAT;
    } else if (poc_type == 1) {
        b.read(1);
        b.se(); b.se();
        const uint32_t cycle = b.ue();
        if (cycle > 255) return VIDEO_DECODER_ERR_FORMAT;
        for (uint32_t i = 0; i < cycle; ++i) b.se();
    } else if (poc_type != 2) {
        return VIDEO_DECODER_ERR_FORMAT;
    }
    /* Bound decoder reference surfaces before OpenH264 can allocate them. */
    if (b.ue() > 4) return VIDEO_DECODER_ERR_LIMIT;
    b.read(1);
    const uint32_t width_mbs_minus1 = b.ue();
    const uint32_t height_mbs_minus1 = b.ue();
    if (!b.ok() || width_mbs_minus1 >= 120 || height_mbs_minus1 >= 120)
        return VIDEO_DECODER_ERR_LIMIT;
    const bool progressive = b.read(1) != 0;
    if (!progressive) return VIDEO_DECODER_ERR_FORMAT;
    b.read(1); // direct_8x8_inference_flag
    uint32_t left = 0, right = 0, top = 0, bottom = 0;
    if (b.read(1)) { left = b.ue(); right = b.ue(); top = b.ue(); bottom = b.ue(); }
    if (!b.ok()) return VIDEO_DECODER_ERR_FORMAT;
    const uint32_t coded_w = (width_mbs_minus1 + 1U) * 16U;
    const uint32_t coded_h = (height_mbs_minus1 + 1U) * 16U;
    const uint64_t crop_w = (uint64_t(left) + right) * 2U;
    const uint64_t crop_h = (uint64_t(top) + bottom) * 2U;
    if (crop_w >= coded_w || crop_h >= coded_h) return VIDEO_DECODER_ERR_FORMAT;
    const uint32_t width = coded_w - uint32_t(crop_w);
    const uint32_t height = coded_h - uint32_t(crop_h);
    const uint32_t padded_limit_w = (config.max_input_width + 15U) & ~15U;
    const uint32_t padded_limit_h = (config.max_input_height + 15U) & ~15U;
    if (width > config.max_input_width || height > config.max_input_height ||
        coded_w > padded_limit_w || coded_h > padded_limit_h ||
        uint64_t(width) * height > kMaxVisiblePixels ||
        uint64_t(coded_w) * coded_h > kMaxCodedPixels)
        return VIDEO_DECODER_ERR_LIMIT;

    info = SpsInfo{};
    info.width = uint16_t(width);
    info.height = uint16_t(height);
    /* H.264 unspecified matrix: use BT.601, the common I420 conversion default. */
    if (b.read(1)) { // vui_parameters_present_flag
        if (b.read(1)) { // aspect_ratio_info_present_flag
            const uint32_t aspect = b.read(8);
            static const uint16_t sar[][2] = {
                {1,1},{1,1},{12,11},{10,11},{16,11},{40,33},{24,11},
                {20,11},{32,11},{80,33},{18,11},{15,11},{64,33},
                {160,99},{4,3},{3,2},{2,1}
            };
            if (aspect == 255) {
                info.sar_width = uint16_t(b.read(16));
                info.sar_height = uint16_t(b.read(16));
                if (!info.sar_width || !info.sar_height) return VIDEO_DECODER_ERR_FORMAT;
            } else if (aspect < sizeof(sar) / sizeof(sar[0])) {
                info.sar_width = sar[aspect][0];
                info.sar_height = sar[aspect][1];
            } else return VIDEO_DECODER_ERR_FORMAT;
        }
        if (b.read(1)) b.read(1); // overscan
        if (b.read(1)) { // video_signal_type_present_flag
            b.read(3);
            info.full_range = b.read(1) != 0;
            if (b.read(1)) {
                b.read(8); b.read(8); // colour primaries / transfer
                const uint32_t matrix = b.read(8);
                if (matrix == 1) info.bt709 = true;
                else if (matrix != 2 && matrix != 5 && matrix != 6)
                    return VIDEO_DECODER_ERR_FORMAT;
            }
        }
        // Remaining VUI fields cannot change the coded/reference allocation
        // bounds above; the codec performs its complete syntax validation.
    }
    if (!b.ok()) return VIDEO_DECODER_ERR_FORMAT;
    info.valid = true;
    sps_id = uint8_t(id);
    return VIDEO_DECODER_OK;
}

/* PPS and slice headers only need their first few Exp-Golomb fields here. */
bool nal_first_ids(const uint8_t *nal, size_t size, unsigned count, uint32_t *ids) {
    uint8_t prefix[64];
    const size_t n = std::min(size, sizeof(prefix));
    size_t out_n = 0;
    if (n < 2) return false;
    unsigned zeros = 0;
    for (size_t i = 1; i < n; ++i) {
        if (zeros >= 2 && nal[i] == 3) { zeros = 0; continue; }
        prefix[out_n++] = nal[i];
        zeros = nal[i] == 0 ? zeros + 1 : 0;
    }
    Bits b(prefix, out_n);
    for (unsigned i = 0; i < count; ++i) ids[i] = b.ue();
    return b.ok();
}

size_t find_start(const uint8_t *data, size_t n, size_t from, size_t &prefix) {
    for (size_t i = from; i + 2 < n; ++i) {
        if (data[i] == 0 && data[i + 1] == 0) {
            if (data[i + 2] == 1) { prefix = 3; return i; }
            if (i + 3 < n && data[i + 2] == 0 && data[i + 3] == 1) {
                prefix = 4;
                return i;
            }
        }
    }
    return n;
}

int validate_au(const uint8_t *data, size_t n,
                const video_decoder_config_t &config, ParameterSets &sets) {
    size_t prefix = 0;
    size_t pos = find_start(data, n, 0, prefix);
    if (pos == n) return VIDEO_DECODER_ERR_FORMAT;
    for (size_t i = 0; i < pos; ++i)
        if (data[i] != 0) return VIDEO_DECODER_ERR_FORMAT;
    unsigned nal_count = 0;
    while (pos < n) {
        if (++nal_count > 1024) return VIDEO_DECODER_ERR_LIMIT;
        const size_t begin = pos + prefix;
        size_t next_prefix = 0;
        const size_t next = find_start(data, n, begin, next_prefix);
        size_t end = next;
        while (end > begin && data[end - 1] == 0) --end;
        if (end <= begin || (data[begin] & 0x80)) return VIDEO_DECODER_ERR_FORMAT;
        const uint8_t type = data[begin] & 31;
        const uint8_t *nal = data + begin;
        const size_t size = end - begin;
        if (type == 7) {
            uint8_t id = 0;
            SpsInfo sps;
            const int rc = parse_sps(nal, size, config, id, sps);
            if (rc) return rc;
            sets.sps[id] = sps;
        } else if (type == 8) {
            uint32_t ids[2];
            if (size > kMaxParameterBytes || !nal_first_ids(nal, size, 2, ids) ||
                ids[0] >= 256 || ids[1] >= 32) return VIDEO_DECODER_ERR_FORMAT;
            if (!sets.sps[ids[1]].valid) return VIDEO_DECODER_ERR_NEEDS_SPS;
            sets.pps_sps[ids[0]] = uint8_t(ids[1]);
            sets.pps_valid[ids[0]] = true;
        } else if (type == 1 || type == 5) {
            uint32_t ids[3];
            if (!nal_first_ids(nal, size, 3, ids) || ids[2] >= 256)
                return VIDEO_DECODER_ERR_FORMAT;
            if (!sets.pps_valid[ids[2]]) return VIDEO_DECODER_ERR_NEEDS_SPS;
            sets.active_sps = sets.pps_sps[ids[2]];
        } else if (type != 6 && type != 9 && type != 10 && type != 11 && type != 12) {
            // No MVC/SVC, partitioned slices, or alternate SPS can bypass bounds.
            return VIDEO_DECODER_ERR_FORMAT;
        }
        pos = next;
        prefix = next_prefix;
    }
    return VIDEO_DECODER_OK;
}

void *allocate_pixels(size_t size) {
#ifdef ESP_PLATFORM
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return std::malloc(size);
#endif
}

uint8_t clamp8(int x) { return uint8_t(std::max(0, std::min(255, x))); }

uint16_t yuv565(int y, int u, int v, bool full, bool bt709) {
    u -= 128; v -= 128;
    int red, green, blue;
    if (full) {
        const int base = y * 256;
        red = (base + (bt709 ? 403 : 359) * v + 128) >> 8;
        green = (base - (bt709 ? 48 : 88) * u - (bt709 ? 120 : 183) * v + 128) >> 8;
        blue = (base + (bt709 ? 475 : 454) * u + 128) >> 8;
    } else {
        const int base = std::max(0, y - 16) * 298;
        red = (base + (bt709 ? 459 : 409) * v + 128) >> 8;
        green = (base - (bt709 ? 55 : 100) * u - (bt709 ? 136 : 208) * v + 128) >> 8;
        blue = (base + (bt709 ? 541 : 516) * u + 128) >> 8;
    }
    return uint16_t((uint16_t(clamp8(red) & 0xf8) << 8) |
                    (uint16_t(clamp8(green) & 0xfc) << 3) |
                    (clamp8(blue) >> 3));
}
} // namespace

struct video_decoder {
    video_decoder_config_t config{};
    ISVCDecoder *codec = nullptr;
    uint16_t *pixels = nullptr;
    uint16_t *sample_x = nullptr;
    size_t pixel_bytes = 0;
    ParameterSets sets{};
    uint64_t frames = 0;
    bool flushed = false;
};

namespace {
void destroy_codec(video_decoder_t *d) {
    if (d->codec) {
        d->codec->Uninitialize();
        WelsDestroyDecoder(d->codec);
        d->codec = nullptr;
    }
}

int initialize_codec(video_decoder_t *d) {
    if (WelsCreateDecoder(&d->codec) != 0 || !d->codec) {
        destroy_codec(d);
        return VIDEO_DECODER_ERR_MEMORY;
    }
    int threads = 0, trace = WELS_LOG_ERROR;
    const long thread_option = d->codec->SetOption(DECODER_OPTION_NUM_OF_THREADS, &threads);
    const long trace_option = d->codec->SetOption(DECODER_OPTION_TRACE_LEVEL, &trace);
    if (thread_option != 0 || trace_option != 0) {
        destroy_codec(d);
        return thread_option == cmMallocMemeError || trace_option == cmMallocMemeError
            ? VIDEO_DECODER_ERR_MEMORY : VIDEO_DECODER_ERR_CODEC;
    }
    SDecodingParam parameters{};
    parameters.uiTargetDqLayer = 255;
    parameters.eEcActiveIdc = ERROR_CON_DISABLE;
    parameters.sVideoProperty.size = sizeof(parameters.sVideoProperty);
    parameters.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
    if (d->codec->Initialize(&parameters) != 0) {
        destroy_codec(d);
        return VIDEO_DECODER_ERR_MEMORY;
    }
    return VIDEO_DECODER_OK;
}

int emit_frame(video_decoder_t *d, uint8_t *planes[3], const SBufferInfo &buffer) {
    if (!buffer.iBufferStatus) return VIDEO_DECODER_OK;
    const auto &image = buffer.UsrData.sSystemBuffer;
    if (!planes[0] || !planes[1] || !planes[2] ||
        image.iWidth <= 0 || image.iHeight <= 0 ||
        image.iWidth > d->config.max_input_width ||
        image.iHeight > d->config.max_input_height ||
        uint64_t(image.iWidth) * image.iHeight > kMaxVisiblePixels ||
        image.iStride[0] < image.iWidth || image.iStride[1] < (image.iWidth + 1) / 2)
        return VIDEO_DECODER_ERR_CODEC;
    const auto &sps = d->sets.sps[d->sets.active_sps];
    const uint32_t sw = image.iWidth, sh = image.iHeight;
    const uint32_t pw = d->config.panel_width, ph = d->config.panel_height;
    const uint64_t aspect_w = uint64_t(sw) * sps.sar_width;
    const uint64_t aspect_h = uint64_t(sh) * sps.sar_height;
    uint32_t dw, dh;
    if (uint64_t(pw) * aspect_h <= uint64_t(ph) * aspect_w) {
        dw = pw;
        dh = uint32_t(std::max(uint64_t(1), uint64_t(pw) * aspect_h / aspect_w));
    } else {
        dh = ph;
        dw = uint32_t(std::max(uint64_t(1), uint64_t(ph) * aspect_w / aspect_h));
    }
    const uint32_t left = (pw - dw) / 2, top = (ph - dh) / 2;
    std::memset(d->pixels, 0, d->pixel_bytes);
    for (uint32_t x = 0; x < dw; ++x) d->sample_x[x] = uint16_t(x * sw / dw);
    for (uint32_t y = 0; y < dh; ++y) {
        const uint32_t sy = y * sh / dh;
        const uint8_t *yp = planes[0] + size_t(sy) * image.iStride[0];
        const uint8_t *up = planes[1] + size_t(sy / 2) * image.iStride[1];
        const uint8_t *vp = planes[2] + size_t(sy / 2) * image.iStride[1];
        uint16_t *dst = d->pixels + size_t(y + top) * pw + left;
        for (uint32_t x = 0; x < dw; ++x) {
            const uint32_t sx = d->sample_x[x];
            dst[x] = yuv565(yp[sx], up[sx / 2], vp[sx / 2], sps.full_range, sps.bt709);
        }
    }
    video_decoder_frame_t frame{};
    frame.rgb565 = d->pixels;
    frame.width = uint16_t(pw); frame.height = uint16_t(ph);
    frame.stride_bytes = size_t(pw) * sizeof(uint16_t);
    frame.view_rect = {uint16_t(left), uint16_t(top), uint16_t(dw), uint16_t(dh)};
    frame.source_width = uint16_t(sw); frame.source_height = uint16_t(sh);
    frame.frame_number = ++d->frames;
    d->config.callback(d->config.user, &frame);
    return VIDEO_DECODER_OK;
}

int decode_result(DECODING_STATE state) {
    if (state & dsOutOfMemory) return VIDEO_DECODER_ERR_MEMORY;
    // Pending is expected during parameter-only input and B-frame reordering.
    return (int(state) & ~int(dsFramePending)) ? VIDEO_DECODER_ERR_CODEC : VIDEO_DECODER_OK;
}
} // namespace

extern "C" int video_decoder_init(const video_decoder_config_t *config,
                                   video_decoder_t **out_decoder) {
    if (!out_decoder) return VIDEO_DECODER_ERR_ARGUMENT;
    *out_decoder = nullptr;
    if (!config || !config->callback || !config->panel_width || !config->panel_height ||
        config->panel_width > kMaxDimension || config->panel_height > kMaxDimension ||
        uint64_t(config->panel_width) * config->panel_height > kMaxVisiblePixels)
        return VIDEO_DECODER_ERR_ARGUMENT;
    video_decoder_config_t c = *config;
    if (!c.max_input_width) c.max_input_width = 1280;
    if (!c.max_input_height) c.max_input_height = 1280;
    if (!c.max_access_unit_bytes) c.max_access_unit_bytes = kDefaultAuBytes;
    if (c.max_input_width > kMaxDimension || c.max_input_height > kMaxDimension ||
        c.max_access_unit_bytes > kHardAuBytes) return VIDEO_DECODER_ERR_LIMIT;
    video_decoder_t *d = new (std::nothrow) video_decoder_t;
    if (!d) return VIDEO_DECODER_ERR_MEMORY;
    d->config = c;
    d->pixel_bytes = size_t(c.panel_width) * c.panel_height * sizeof(uint16_t);
    d->pixels = static_cast<uint16_t *>(allocate_pixels(d->pixel_bytes + size_t(c.panel_width) * sizeof(uint16_t)));
    if (!d->pixels) { delete d; return VIDEO_DECODER_ERR_MEMORY; }
    d->sample_x = d->pixels + size_t(c.panel_width) * c.panel_height;
    const int rc = initialize_codec(d);
    if (rc) { video_decoder_destroy(d); return rc; }
    *out_decoder = d;
    return VIDEO_DECODER_OK;
}

extern "C" int video_decoder_push(video_decoder_t *d, const uint8_t *annexb, size_t length) {
    if (!d || !d->codec || !annexb || !length || d->flushed)
        return VIDEO_DECODER_ERR_ARGUMENT;
    if (length > d->config.max_access_unit_bytes || length > INT_MAX)
        return VIDEO_DECODER_ERR_LIMIT;
    ParameterSets pending = d->sets;
    const int validation = validate_au(annexb, length, d->config, pending);
    if (validation) return validation;
    d->sets = pending;
    uint8_t *planes[3] = {};
    SBufferInfo info{};
    const DECODING_STATE state = d->codec->DecodeFrameNoDelay(annexb, int(length), planes, &info);
    const int rc = decode_result(state);
    if (rc) return rc; // Error concealment is disabled; never fabricate a frame.
    return emit_frame(d, planes, info);
}

extern "C" int video_decoder_flush(video_decoder_t *d) {
    if (!d || !d->codec) return VIDEO_DECODER_ERR_ARGUMENT;
    if (d->flushed) return VIDEO_DECODER_OK;
    d->flushed = true;
    int eos = 1;
    if (d->codec->SetOption(DECODER_OPTION_END_OF_STREAM, &eos) != 0)
        return VIDEO_DECODER_ERR_CODEC;
    int remaining = 0;
    if (d->codec->GetOption(DECODER_OPTION_NUM_OF_FRAMES_REMAINING_IN_BUFFER, &remaining) != 0 ||
        remaining < 0 || remaining > 32) return VIDEO_DECODER_ERR_CODEC;
    for (int i = 0; i < remaining; ++i) {
        uint8_t *planes[3] = {};
        SBufferInfo info{};
        const int rc = decode_result(d->codec->FlushFrame(planes, &info));
        if (rc) return rc;
        const int output = emit_frame(d, planes, info);
        if (output) return output;
    }
    return VIDEO_DECODER_OK;
}

extern "C" int video_decoder_reset(video_decoder_t *d) {
    if (!d) return VIDEO_DECODER_ERR_ARGUMENT;
    destroy_codec(d);
    d->sets = ParameterSets{};
    d->frames = 0;
    d->flushed = false;
    return initialize_codec(d);
}

extern "C" void video_decoder_destroy(video_decoder_t *d) {
    if (!d) return;
    destroy_codec(d);
    std::free(d->pixels);
    delete d;
}

extern "C" const char *video_decoder_error_string(int result) {
    switch (result) {
    case VIDEO_DECODER_OK: return "accepted";
    case VIDEO_DECODER_ERR_ARGUMENT: return "invalid argument or ended session";
    case VIDEO_DECODER_ERR_MEMORY: return "decoder or PSRAM allocation failed";
    case VIDEO_DECODER_ERR_FORMAT: return "unsupported or malformed AVC bitstream";
    case VIDEO_DECODER_ERR_LIMIT: return "video exceeds configured resource limits";
    case VIDEO_DECODER_ERR_CODEC: return "H.264 decoder rejected frame; reset and request IDR";
    case VIDEO_DECODER_ERR_NEEDS_SPS: return "SPS/PPS required before slices";
    default: return "unknown decoder error";
    }
}
