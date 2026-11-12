// Verification of the GoldSrc SPR sprite decoder.
//
// Builds synthetic sprites in memory and checks the decoded result. This
// exercises the parts that are easy to get silently wrong: the 40-byte header
// field order, the int32 frame-type word that precedes every frame, group
// frames (type 1) with their interval table, the raw w*h palette indices, and
// the per-blend-mode meaning of palette index 255.
//
// The regression this guards against: the parser used to read the frame header
// without skipping the frame-type word, so origin_y landed in width. With a
// negative origin_y (215 of the 7,634 shipped frames) width became negative
// and the parser returned zero frames.
//
// No framework: prints its checks and exits non-zero on failure, so `ctest`
// and a developer running the binary by hand see the same output.

#include "parsers/goldsrc/spr_parser.h"

#include <cstdio>
#include <cstring>
#include <span>
#include <vector>

using namespace quebratsk::parsers::goldsrc;

static int failures = 0;

static void check_i(const char* what, long long got, long long want) {
    if (got != want) {
        std::printf("  FAIL %-46s got %lld want %lld\n", what, got, want);
        ++failures;
    }
}

static void check_u8(const char* what, int got, int want) {
    if (got != want) {
        std::printf("  FAIL %-46s got %d want %d\n", what, got, want);
        ++failures;
    }
}

// ------------------------------------------------------------------ builder ----
struct Builder {
    std::vector<uint8_t> buf;

    void ensure(size_t n) { if (buf.size() < n) buf.resize(n, 0); }
    template <typename T> void put(size_t at, const T& value) {
        ensure(at + sizeof(T));
        std::memcpy(buf.data() + at, &value, sizeof(T));
    }

    void begin(int32_t sprite_type, int32_t texture_format, float radius,
               int32_t width, int32_t height, int32_t num_frames,
               float beam_length, int32_t sync_type) {
        buf.assign(42 + 768, 0);
        std::memcpy(buf.data(), "IDSP", 4);
        put<int32_t>(4, 2);
        put<int32_t>(8, sprite_type);
        put<int32_t>(12, texture_format);
        put<float>(16, radius);
        put<int32_t>(20, width);
        put<int32_t>(24, height);
        put<int32_t>(28, num_frames);
        put<float>(32, beam_length);
        put<int32_t>(36, sync_type);
        put<uint16_t>(40, 256);
    }

    void palette_entry(int index, uint8_t r, uint8_t g, uint8_t b) {
        buf[42 + index * 3 + 0] = r;
        buf[42 + index * 3 + 1] = g;
        buf[42 + index * 3 + 2] = b;
    }

    void frame_type(int32_t type) {
        size_t at = buf.size();
        buf.resize(at + 4);
        std::memcpy(buf.data() + at, &type, 4);
    }

    void group_count(int32_t count) {
        size_t at = buf.size();
        buf.resize(at + 4);
        std::memcpy(buf.data() + at, &count, 4);
    }

    void intervals(const std::vector<float>& values) {
        size_t at = buf.size();
        buf.resize(at + values.size() * 4);
        std::memcpy(buf.data() + at, values.data(), values.size() * 4);
    }

    void frame(int32_t origin_x, int32_t origin_y, int32_t width, int32_t height,
               const std::vector<uint8_t>& pixels) {
        size_t at = buf.size();
        buf.resize(at + 16 + pixels.size());
        std::memcpy(buf.data() + at + 0, &origin_x, 4);
        std::memcpy(buf.data() + at + 4, &origin_y, 4);
        std::memcpy(buf.data() + at + 8, &width, 4);
        std::memcpy(buf.data() + at + 12, &height, 4);
        std::memcpy(buf.data() + at + 16, pixels.data(), pixels.size());
    }

    std::expected<ParsedSprite, SPRParseError> parse() const {
        auto bytes = std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(buf.data()), buf.size());
        return SPRParser::parse(bytes);
    }
};

int main() {
    std::printf("GoldSrc SPR parser verification\n\n");

    // ---------------------------------------------------- single frame decode ----
    {
        Builder b;
        b.begin(/*type*/ 2, /*tex_format*/ 3, /*radius*/ 5.0f,
                /*width*/ 2, /*height*/ 2, /*frames*/ 1, /*beam*/ 0.0f, /*sync*/ 1);
        b.palette_entry(0, 10, 20, 30);
        b.palette_entry(1, 40, 50, 60);
        b.palette_entry(255, 255, 0, 255);
        b.frame_type(0);
        b.frame(-1, -1, 2, 2, {0, 1, 255, 0});

        auto parsed = b.parse();
        check_i("single: parse ok", parsed.has_value() ? 1 : 0, 1);
        if (parsed) {
            check_i("single: frames", (long long)parsed->frames.size(), 1);
            check_i("single: complete", parsed->complete ? 1 : 0, 1);
            check_i("single: tex_format", parsed->tex_format, 3);
            const auto& f = parsed->frames.at(0);
            check_i("single: width", f.width, 2);
            check_i("single: height", f.height, 2);
            check_i("single: origin_x", f.origin_x, -1);
            check_i("single: origin_y", f.origin_y, -1);
            check_i("single: rgba bytes", (long long)f.rgba8_pixels.size(), 16);
            check_u8("single: pixel0 r", f.rgba8_pixels[0], 10);
            check_u8("single: pixel0 g", f.rgba8_pixels[1], 20);
            check_u8("single: pixel0 b", f.rgba8_pixels[2], 30);
            check_u8("single: pixel0 a", f.rgba8_pixels[3], 255);
            check_u8("single: pixel1 r", f.rgba8_pixels[4], 40);
            check_u8("single: pixel1 a", f.rgba8_pixels[7], 255);
            check_u8("single: pixel2 r (index 255)", f.rgba8_pixels[8], 255);
            check_u8("single: pixel2 a (index 255)", f.rgba8_pixels[11], 0);
        }
    }

    // ------------------------------------------------ alpha depends on blend mode ----
    // Same pixels, four blend modes. Index 255 is only transparent in alpha-test.
    for (int32_t format = 0; format < 4; ++format) {
        Builder b;
        b.begin(2, format, 1.0f, 2, 1, 1, 0.0f, 0);
        b.palette_entry(100, 1, 2, 3);
        b.palette_entry(255, 200, 150, 50);
        b.frame_type(0);
        b.frame(-1, 1, 2, 1, {100, 255});

        auto parsed = b.parse();
        check_i("blend: parse ok", parsed.has_value() ? 1 : 0, 1);
        if (!parsed || parsed->frames.size() != 1) continue;
        const auto& px = parsed->frames.at(0).rgba8_pixels;
        if (format == 2) { // index alpha: colour is palette[255], alpha is the index
            check_u8("indexalpha: pixel0 r", px[0], 200);
            check_u8("indexalpha: pixel0 a", px[3], 100);
            check_u8("indexalpha: pixel1 r", px[4], 200);
            check_u8("indexalpha: pixel1 a", px[7], 255);
        } else {
            check_u8("blend: pixel0 r", px[0], 1);
            check_u8("blend: pixel0 a", px[3], 255);
            check_u8("blend: pixel1 r (index 255)", px[4], 200);
            check_u8("blend: pixel1 a (index 255)", px[7], format == 3 ? 0 : 255);
        }
    }

    // ------------------------------------- negative origin_y (the old failure) ----
    {
        Builder b;
        b.begin(2, 0, 4.0f, 4, 4, 1, 0.0f, 0);
        b.palette_entry(0, 1, 2, 3);
        b.frame_type(0);
        b.frame(-2, -4, 4, 4, std::vector<uint8_t>(16, 0));

        auto parsed = b.parse();
        check_i("negative origin_y: parse ok", parsed.has_value() ? 1 : 0, 1);
        if (parsed) {
            // The old parser read the frame-type word as origin_x, so origin_y
            // (-4) became width and the frame was dropped.
            check_i("negative origin_y: frames", (long long)parsed->frames.size(), 1);
            check_i("negative origin_y: width", parsed->frames.at(0).width, 4);
            check_i("negative origin_y: origin_y", parsed->frames.at(0).origin_y, -4);
        }
    }

    // ------------------------------------------------------- two frames, offsets ----
    {
        Builder b;
        b.begin(2, 0, 1.0f, 3, 2, 2, 0.0f, 0);
        b.palette_entry(2, 70, 80, 90);
        b.palette_entry(3, 100, 110, 120);
        b.frame_type(0);
        b.frame(0, 0, 2, 2, {0, 0, 0, 2});
        b.frame_type(0);
        b.frame(-1, 1, 3, 1, {3, 3, 3});

        auto parsed = b.parse();
        check_i("two frames: parse ok", parsed.has_value() ? 1 : 0, 1);
        if (parsed) {
            check_i("two frames: count", (long long)parsed->frames.size(), 2);
            check_i("two frames: f0 width", parsed->frames.at(0).width, 2);
            check_i("two frames: f1 width", parsed->frames.at(1).width, 3);
            check_i("two frames: f1 origin_y", parsed->frames.at(1).origin_y, 1);
            check_u8("two frames: f0 last pixel g", parsed->frames.at(0).rgba8_pixels[15 - 2],
                     80);
            check_u8("two frames: f1 first pixel r",
                     parsed->frames.at(1).rgba8_pixels[0], 100);
        }
    }

    // ------------------------------------------------------------ grouped frame ----
    {
        Builder b;
        b.begin(2, 0, 1.0f, 1, 1, 1, 0.0f, 0);
        b.palette_entry(5, 11, 22, 33);
        b.palette_entry(6, 44, 55, 66);
        b.palette_entry(7, 77, 88, 99);
        b.frame_type(1);
        b.group_count(3);
        b.intervals({0.1f, 0.2f, 0.5f});
        b.frame(-1, 0, 1, 1, {5});
        b.frame(0, 0, 1, 1, {6});
        b.frame(1, 0, 1, 1, {7});

        auto parsed = b.parse();
        check_i("group: parse ok", parsed.has_value() ? 1 : 0, 1);
        if (parsed) {
            check_i("group: members decoded", (long long)parsed->frames.size(), 3);
            check_i("group: member 0 origin_x", parsed->frames.at(0).origin_x, -1);
            check_i("group: member 2 origin_x", parsed->frames.at(2).origin_x, 1);
            check_u8("group: member 1 r", parsed->frames.at(1).rgba8_pixels[0], 44);
            check_u8("group: member 2 b", parsed->frames.at(2).rgba8_pixels[2], 99);
            check_i("group: member 2 interval ms",
                    (long long)(parsed->frames.at(2).group_interval * 1000.0f + 0.5f), 500);
            check_i("group: complete", parsed->complete ? 1 : 0, 1);
        }
    }

    // ---------------------------------------------------------------- truncation ----
    {
        Builder b;
        b.begin(2, 0, 1.0f, 2, 2, 1, 0.0f, 0);
        b.frame_type(0);
        b.frame(0, 0, 2, 2, {0, 1});
        auto parsed = b.parse();
        check_i("truncated: parse ok", parsed.has_value() ? 1 : 0, 1);
        if (parsed) {
            check_i("truncated: no partial frame", (long long)parsed->frames.size(), 0);
            check_i("truncated: flagged incomplete", parsed->complete ? 1 : 0, 0);
        }
    }

    // ------------------------------------------------------ bad version, zero group ----
    {
        Builder b;
        b.begin(2, 0, 1.0f, 1, 1, 1, 0.0f, 0);
        std::memcpy(b.buf.data() + 4, "\x03\x00\x00\x00", 4);
        auto parsed = b.parse();
        check_i("bad version: rejected",
                parsed.has_value() ? 0 : static_cast<int>(parsed.error()) ==
                                            static_cast<int>(SPRParseError::VersionMismatch),
                1);
    }
    {
        Builder b;
        b.begin(2, 0, 1.0f, 1, 1, 1, 0.0f, 0);
        b.frame_type(1);
        b.group_count(0);
        auto parsed = b.parse();
        check_i("zero-member group: parse ok", parsed.has_value() ? 1 : 0, 1);
        if (parsed) {
            check_i("zero-member group: no frames", (long long)parsed->frames.size(), 0);
            check_i("zero-member group: flagged incomplete", parsed->complete ? 1 : 0, 0);
        }
    }

    if (failures == 0) {
        std::printf("all checks passed\n");
        return 0;
    }
    std::printf("%d check(s) failed\n", failures);
    return 1;
}
