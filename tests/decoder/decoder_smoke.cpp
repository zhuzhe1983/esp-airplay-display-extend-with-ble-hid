#include "video_decoder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    std::exit(1); } } while (0)

static std::vector<uint8_t> read_file(const std::string &name) {
    std::ifstream file(std::string(FIXTURE_DIR) + "/" + name, std::ios::binary);
    CHECK(file.good());
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
}

struct Nal { size_t start, payload, end; uint8_t type; };
static std::vector<Nal> nals(const std::vector<uint8_t> &bytes) {
    std::vector<Nal> result;
    for (size_t i = 0; i + 3 < bytes.size();) {
        unsigned prefix = 0;
        if (bytes[i] == 0 && bytes[i+1] == 0) {
            if (bytes[i+2] == 1) prefix = 3;
            else if (bytes[i+2] == 0 && bytes[i+3] == 1) prefix = 4;
        }
        if (!prefix) { ++i; continue; }
        if (!result.empty()) result.back().end = i;
        CHECK(i + prefix < bytes.size());
        result.push_back({i, i+prefix, bytes.size(), uint8_t(bytes[i+prefix] & 31)});
        i += prefix + 1;
    }
    return result;
}

struct Capture {
    std::vector<uint8_t> reference;
    unsigned frames = 0;
    unsigned panel_w = 80;
    unsigned skip_outputs=0,decisions=0;
    unsigned panel_h = 80;
    uint64_t error_sum = 0;
    unsigned max_error = 0, comparisons = 0;
    std::set<uint64_t> hashes;
};

struct LatestYuv {
    video_decoder_yuv_frame_t frame{};
    std::vector<uint8_t> pixels;
    unsigned count=0;
};

static void on_yuv(void *user,const video_decoder_yuv_frame_t *frame){
    auto &latest=*static_cast<LatestYuv *>(user);
    CHECK(frame&&frame->width==160&&frame->height==96&&frame->frame_number==latest.count+1);
    const size_t cw=(frame->width+1U)/2,ch=(frame->height+1U)/2;
    latest.pixels.resize(size_t(frame->width)*frame->height+2*cw*ch);
    latest.frame=*frame;
    uint8_t *dst=latest.pixels.data();
    for(unsigned plane=0;plane<3;++plane){
        const size_t rows=plane?ch:frame->height,cols=plane?cw:frame->width;
        latest.frame.planes[plane]=dst;latest.frame.strides[plane]=cols;
        for(size_t y=0;y<rows;++y)
            std::copy_n(frame->planes[plane]+y*frame->strides[plane],cols,dst+y*cols);
        dst+=rows*cols;
    }
    ++latest.count;
}

static void on_frame(void *user, const video_decoder_frame_t *frame) {
    auto &capture = *static_cast<Capture *>(user);
    CHECK(frame && frame->rgb565);
    CHECK(frame->width == capture.panel_w && frame->height == capture.panel_h);
    CHECK(frame->source_width == 160 && frame->source_height == 96);
    CHECK(frame->stride_bytes == capture.panel_w*2);
    CHECK(frame->frame_number == capture.frames + 1);
    const unsigned view_h=capture.panel_w*96/160;
    CHECK(frame->view_rect.x == 0 && frame->view_rect.width == capture.panel_w);
    CHECK(frame->view_rect.height == view_h);
    CHECK(frame->view_rect.y == (capture.panel_h - view_h) / 2);
    CHECK(capture.frames < 8);
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned y = 0; y < capture.panel_h; ++y) {
        for (unsigned x = 0; x < capture.panel_w; ++x) {
            const uint16_t pixel = frame->rgb565[y * capture.panel_w + x];
            if (y < frame->view_rect.y || y >= unsigned(frame->view_rect.y) + view_h)
                CHECK(pixel == 0);
            hash = (hash ^ pixel) * 1099511628211ULL;
        }
    }
    capture.hashes.insert(hash);
    unsigned sample = (capture.frames+capture.skip_outputs) * 40 * 3;
    for (unsigned y = 5; y < 48; y += 10) {
        for (unsigned x = 5; x < 80; x += 10) {
            const unsigned ox=x*capture.panel_w/80,oy=y*view_h/48;
            const uint16_t p = frame->rgb565[(oy + frame->view_rect.y) * capture.panel_w + ox];
            const unsigned rgb[] = {
                unsigned((p >> 11) & 31) * 255 / 31,
                unsigned((p >> 5) & 63) * 255 / 63,
                unsigned(p & 31) * 255 / 31};
            for (unsigned c = 0; c < 3; ++c) {
                CHECK(sample < capture.reference.size());
                const unsigned error = unsigned(std::abs(int(rgb[c]) - int(capture.reference[sample++])));
                capture.error_sum += error;
                capture.max_error = std::max(capture.max_error, error);
                ++capture.comparisons;
            }
        }
    }
    ++capture.frames;
}

static void push_stream(video_decoder_t *decoder, const std::vector<uint8_t> &data) {
    size_t begin = 0;
    bool have_aud = false;
    for (const auto &nal : nals(data)) {
        if (nal.type == 9) {
            if (have_aud) {
                const int rc = video_decoder_push(decoder, data.data() + begin, nal.start - begin);
                if (rc) std::fprintf(stderr, "push: %s\n", video_decoder_error_string(rc));
                CHECK(rc == VIDEO_DECODER_OK);
            }
            begin = nal.start;
            have_aud = true;
        }
    }
    CHECK(have_aud);
    const int rc = video_decoder_push(decoder, data.data() + begin, data.size() - begin);
    if (rc) std::fprintf(stderr, "last push: %s\n", video_decoder_error_string(rc));
    CHECK(rc == VIDEO_DECODER_OK);
    CHECK(video_decoder_flush(decoder) == VIDEO_DECODER_OK);
    CHECK(video_decoder_flush(decoder) == VIDEO_DECODER_OK);
}

static void run_profile(const std::string &profile) {
    const auto data = read_file(profile + ".h264");
    const auto nal_units = nals(data);
    Capture capture;
    capture.reference = read_file(profile + ".rgb.samples");
    video_decoder_config_t config{};
    config.panel_width = 80; config.panel_height = 80;
    config.callback = on_frame; config.user = &capture;
    video_decoder_t *decoder = nullptr;
    CHECK(video_decoder_init(&config, &decoder) == VIDEO_DECODER_OK);
    // Parameter sets arrive separately in AirPlay codec-config packets.
    for (const auto &nal : nal_units) {
        if (nal.type == 7 || nal.type == 8)
            CHECK(video_decoder_push(decoder, data.data() + nal.start, nal.end - nal.start) == VIDEO_DECODER_OK);
    }
    CHECK(capture.frames == 0);
    push_stream(decoder, data);
    CHECK(capture.frames == 8 && capture.hashes.size() >= 6);
    const double mean = double(capture.error_sum) / capture.comparisons;
    std::printf("%s: %u real frames, %zu unique RGB565 frames, FFmpeg sample MAE %.3f/255, max %u\n",
                profile.c_str(), capture.frames, capture.hashes.size(), mean, capture.max_error);
    // RGB565 quantization and independent integer matrix rounding account for
    // small differences; a frozen/missing/misordered decode fails by a wide margin.
    CHECK(mean < 4.5 && capture.max_error <= 40);
    CHECK(video_decoder_push(decoder, data.data(), data.size()) == VIDEO_DECODER_ERR_ARGUMENT);
    CHECK(video_decoder_reset(decoder) == VIDEO_DECODER_OK);
    capture.frames = 0; capture.hashes.clear();
    capture.error_sum = 0; capture.comparisons = 0; capture.max_error = 0;
    push_stream(decoder, data);
    CHECK(capture.frames == 8);
    video_decoder_destroy(decoder);

    // Same picture on a portrait panel must preserve fit and black borders.
    capture.frames = 0; capture.panel_h = 128; config.panel_height = 128;
    CHECK(video_decoder_init(&config, &decoder) == VIDEO_DECODER_OK);
    push_stream(decoder, data);
    CHECK(capture.frames == 8);
    video_decoder_destroy(decoder);

    // Upscaling must preserve reference colors at 1.5x and 2x, including B-frame order.
    for(unsigned size:{240u,320u}){
        capture.frames=0;capture.panel_w=capture.panel_h=size;
        capture.error_sum=capture.comparisons=capture.max_error=0;
        config.panel_width=config.panel_height=uint16_t(size);
        CHECK(video_decoder_init(&config,&decoder)==VIDEO_DECODER_OK);
        push_stream(decoder,data);
        CHECK(capture.frames==8&&double(capture.error_sum)/capture.comparisons<4.5&&capture.max_error<=40);
        video_decoder_destroy(decoder);
    }
    capture.frames=capture.decisions=0;capture.skip_outputs=4;
    capture.error_sum=capture.comparisons=capture.max_error=0;
    config.should_present=[](void *user){
        auto &c=*static_cast<Capture *>(user);return c.decisions++>=c.skip_outputs;
    };
    CHECK(video_decoder_init(&config,&decoder)==VIDEO_DECODER_OK);
    push_stream(decoder,data);
    CHECK(capture.decisions==8&&capture.frames==4);
    CHECK(double(capture.error_sum)/capture.comparisons<4.5&&capture.max_error<=40);
    video_decoder_destroy(decoder);

    // Keep just the latest owned I420 snapshot, then destroy all codec/reference
    // buffers before converting it on an independent consumer.
    LatestYuv latest;
    config.callback=nullptr;config.yuv_callback=on_yuv;config.user=&latest;config.should_present=nullptr;
    CHECK(video_decoder_init(&config,&decoder)==VIDEO_DECODER_OK);
    push_stream(decoder,data);
    CHECK(latest.count==8&&latest.frame.frame_number==8);
    video_decoder_destroy(decoder);
    capture.frames=0;capture.skip_outputs=7;
    capture.error_sum=capture.comparisons=capture.max_error=0;
    video_converter_t *converter=nullptr;
    CHECK(video_converter_init(config.panel_width,config.panel_height,on_frame,&capture,&converter)==VIDEO_DECODER_OK);
    CHECK(video_converter_render(converter,&latest.frame)==VIDEO_DECODER_OK);
    CHECK(capture.frames==1&&double(capture.error_sum)/capture.comparisons<4.5&&capture.max_error<=40);
    latest.frame.strides[2]=0;
    CHECK(video_converter_render(converter,&latest.frame)==VIDEO_DECODER_ERR_ARGUMENT);
    CHECK(video_converter_render(converter,nullptr)==VIDEO_DECODER_ERR_ARGUMENT);
    video_converter_destroy(converter);
}

static void bounds_and_errors() {
    Capture capture;
    video_decoder_config_t config{};
    config.panel_width = 80; config.panel_height = 80;
    config.callback = on_frame; config.user = &capture;
    video_decoder_t *decoder = nullptr;
    CHECK(video_decoder_init(nullptr, &decoder) == VIDEO_DECODER_ERR_ARGUMENT);
    CHECK(video_decoder_init(&config, nullptr) == VIDEO_DECODER_ERR_ARGUMENT);
    config.panel_width = 65535;
    CHECK(video_decoder_init(&config, &decoder) == VIDEO_DECODER_ERR_ARGUMENT);
    config.panel_width = 80; config.max_input_width = 128;
    CHECK(video_decoder_init(&config, &decoder) == VIDEO_DECODER_OK);
    const auto data = read_file("high.h264");
    const auto units = nals(data);
    for (const auto &nal : units) {
        if (nal.type == 7)
            CHECK(video_decoder_push(decoder, data.data() + nal.start, nal.end - nal.start) == VIDEO_DECODER_ERR_LIMIT);
        if (nal.type == 5) {
            CHECK(video_decoder_push(decoder, data.data() + nal.start, nal.end - nal.start) == VIDEO_DECODER_ERR_NEEDS_SPS);
            break;
        }
    }
    const uint8_t junk[] = {0x12, 0x34, 0x56};
    const uint8_t truncated_sps[] = {0, 0, 1, 0x67, 100};
    CHECK(video_decoder_push(decoder, junk, sizeof(junk)) == VIDEO_DECODER_ERR_FORMAT);
    CHECK(video_decoder_push(decoder, truncated_sps, sizeof(truncated_sps)) == VIDEO_DECODER_ERR_FORMAT);
    CHECK(video_decoder_push(decoder, nullptr, 1) == VIDEO_DECODER_ERR_ARGUMENT);
    CHECK(video_decoder_push(decoder, junk, 1024U * 1024U + 1) == VIDEO_DECODER_ERR_LIMIT);
    CHECK(capture.frames == 0);
    video_decoder_destroy(decoder);

    config.max_input_width = 1920; config.max_input_height = 1080;
    CHECK(video_decoder_init(&config, &decoder) == VIDEO_DECODER_OK);
    const auto hd = read_file("1080p.params.h264");
    CHECK(video_decoder_push(decoder, hd.data(), hd.size()) == VIDEO_DECODER_OK);
    const auto refs = read_file("many_refs.params.h264");
    CHECK(video_decoder_push(decoder, refs.data(), refs.size()) == VIDEO_DECODER_ERR_LIMIT);
    CHECK(capture.frames == 0);
    video_decoder_destroy(decoder);
    config.max_input_width = 0; config.max_input_height = 0;
    CHECK(video_decoder_init(&config, &decoder) == VIDEO_DECODER_OK);
    CHECK(video_decoder_push(decoder, hd.data(), hd.size()) == VIDEO_DECODER_ERR_LIMIT);
    video_decoder_destroy(decoder);
    video_decoder_destroy(nullptr);
    std::puts("bounds: malformed input, missing SPS/PPS, dimensions, AU length, EOS and reset passed");
}

int main() {
    bounds_and_errors();
    run_profile("baseline");
    run_profile("main");
    run_profile("high");
    return 0;
}
