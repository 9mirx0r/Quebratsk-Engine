// Verification of the Source texture reader (src/parsers/source1/vtf_parser.cpp).
//
// Every texture here is built by hand, a few bytes at a time, so the expected colour of
// each pixel is known without any game file. What is pinned, from the layout Valve
// documents on the Valve Developer Community "VTF (Valve Texture Format)" page:
//
//   - each uncompressed format puts its channels where its name says (ABGR8888 is
//     alpha, blue, green, red, in that order in memory);
//   - mips are stored smallest first, so the full-size image is the last thing in the
//     file, and the reader must take that one and not the small one before it;
//   - with several frames, frame 0 of the full-size mip is the one returned;
//   - a texture too short for what its header claims is refused, not read past its end;
//   - a format the reader cannot decode is refused, not turned into noise;
//   - inside each mip come every frame, then every face of a cube map (six; seven in
//     files before 7.5 unless their first frame is -1), then every slice;
//   - before 7.3 the image follows the header and its small DXT1 thumbnail; from 7.3 a
//     list of resources after the header says where the image starts, and other data
//     may follow it.
//
// Exits non-zero on failure.
#include "parsers/source1/vtf_parser.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using namespace quebratsk::parsers::source1;

static int failures = 0;

static void fail(const char* what) {
    std::printf("  FAIL %s\n", what);
    ++failures;
}

static void check(const char* what, int got, int want) {
    if (got != want) {
        std::printf("  FAIL %-46s got %3d want %3d\n", what, got, want);
        ++failures;
    }
}

struct Rgba { uint8_t r, g, b, a; };

// Four pixels of a 2x2 image, each channel different from every other one, so a swap of
// any two channels shows.
static const Rgba kPixels[4] = {
    {10, 20, 30, 40}, {50, 60, 70, 80}, {90, 100, 110, 120}, {130, 140, 150, 160}};

static void put(std::vector<std::byte>& b, const void* data, size_t n) {
    const auto* p = static_cast<const std::byte*>(data);
    b.insert(b.end(), p, p + n);
}

/// A 7.2 header of `header_size` bytes, padded with zeros, no thumbnail.
static std::vector<std::byte> header(VTFImageFormat format, uint16_t w, uint16_t h,
                                     uint16_t frames, uint8_t mips,
                                     uint32_t header_size = 80) {
    VTFHeader hd{};
    std::memcpy(hd.magic, "VTF\0", 4);
    hd.version[0] = 7;
    hd.version[1] = 2;
    hd.header_size = header_size;
    hd.width = w;
    hd.height = h;
    hd.frames = frames;
    hd.high_res_image_format = static_cast<uint32_t>(format);
    hd.mipmap_count = mips;
    hd.low_res_image_format = 0xFFFFFFFFu;  // no thumbnail
    std::vector<std::byte> b;
    put(b, &hd, sizeof(hd));
    b.resize(header_size, std::byte{0});
    return b;
}

static void poke16(std::vector<std::byte>& b, size_t at, uint16_t v) {
    std::memcpy(b.data() + at, &v, 2);
}
static void poke32(std::vector<std::byte>& b, size_t at, uint32_t v) {
    std::memcpy(b.data() + at, &v, 4);
}
static void push_n(std::vector<std::byte>& b, size_t n, uint8_t v) {
    b.insert(b.end(), n, std::byte(v));
}

// Header fields, by offset, past the ones VTFHeader names.
constexpr size_t kMinorAt = 8, kFlagsAt = 20, kFirstFrameAt = 26, kResourceCountAt = 68;

/// One pixel written in the memory order of `format`.
static void encode(std::vector<std::byte>& b, VTFImageFormat format, Rgba p) {
    auto push = [&](std::initializer_list<uint8_t> v) {
        for (uint8_t c : v) b.push_back(std::byte(c));
    };
    switch (format) {
        case VTFImageFormat::RGBA8888: push({p.r, p.g, p.b, p.a}); break;
        case VTFImageFormat::ABGR8888: push({p.a, p.b, p.g, p.r}); break;
        case VTFImageFormat::ARGB8888: push({p.g, p.b, p.a, p.r}); break;
        case VTFImageFormat::BGRA8888: push({p.b, p.g, p.r, p.a}); break;
        case VTFImageFormat::BGRX8888: push({p.b, p.g, p.r, 0}); break;
        case VTFImageFormat::RGB888:   push({p.r, p.g, p.b}); break;
        case VTFImageFormat::BGR888:   push({p.b, p.g, p.r}); break;
        case VTFImageFormat::I8:       push({p.r}); break;
        case VTFImageFormat::IA88:     push({p.r, p.a}); break;
        case VTFImageFormat::A8:       push({p.a}); break;
        default: break;
    }
}

/// What the reader should hand back for a pixel stored in `format`.
static Rgba expected(VTFImageFormat format, Rgba p) {
    switch (format) {
        case VTFImageFormat::BGRX8888:
        case VTFImageFormat::RGB888:
        case VTFImageFormat::BGR888:   return {p.r, p.g, p.b, 255};
        case VTFImageFormat::I8:       return {p.r, p.r, p.r, 255};
        case VTFImageFormat::IA88:     return {p.r, p.r, p.r, p.a};
        case VTFImageFormat::A8:       return {255, 255, 255, p.a};
        default:                       return p;
    }
}

static void check_pixels(const char* name, const quebratsk::ir::IRTextureData& tex,
                         VTFImageFormat format) {
    char what[96];
    for (int i = 0; i < 4; ++i) {
        const Rgba want = expected(format, kPixels[i]);
        const uint8_t* got = &tex.rgba8_pixels[static_cast<size_t>(i) * 4];
        const uint8_t wants[4] = {want.r, want.g, want.b, want.a};
        for (int c = 0; c < 4; ++c) {
            std::snprintf(what, sizeof(what), "%s px%d %c", name, i, "rgba"[c]);
            check(what, got[c], wants[c]);
        }
    }
}

/// Every .vtf under `root`, read: how many come back, and for the ones refused, which
/// format and which error. Run by hand with a folder of game textures; nothing from a
/// game is kept in the repository.
static int corpus(const char* root) {
    namespace fs = std::filesystem;
    int files = 0, read = 0;
    std::map<std::string, int> refused;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file()) continue;
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (ext != ".vtf") continue;
        std::ifstream f(it->path(), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
        ++files;
        const auto bytes = std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(text.data()), text.size());
        const auto res = VTFParser::parse(bytes);
        if (res) {
            ++read;
            continue;
        }
        uint32_t minor = 0, format = 0, flags = 0;
        if (text.size() >= 56) {
            std::memcpy(&minor, text.data() + 8, 4);
            std::memcpy(&flags, text.data() + 20, 4);
            std::memcpy(&format, text.data() + 52, 4);
        }
        char key[96];
        std::snprintf(key, sizeof(key), "error %d, format %u, version 7.%u%s",
                      static_cast<int>(res.error()), format, minor,
                      (flags & kVtfFlagEnvmap) ? ", cube map" : "");
        if (refused[key]++ == 0) {
            std::printf("  first of [%s]: %s\n", key, it->path().string().c_str());
        }
    }
    std::printf("\n  %d texture(s), %d read\n", files, read);
    for (const auto& [k, n] : refused) std::printf("  %4d refused: %s\n", n, k.c_str());
    return 0;
}

int main(int argc, char** argv) {
    std::printf("VTF parser verification\n\n");
    if (argc > 1) return corpus(argv[1]);

    // ---- every uncompressed format the reader decodes ---------------------------
    struct Case { const char* name; VTFImageFormat format; bool alpha; };
    const Case cases[] = {
        {"RGBA8888", VTFImageFormat::RGBA8888, true},
        {"ABGR8888", VTFImageFormat::ABGR8888, true},
        {"ARGB8888", VTFImageFormat::ARGB8888, true},
        {"BGRA8888", VTFImageFormat::BGRA8888, true},
        {"BGRX8888", VTFImageFormat::BGRX8888, false},
        {"RGB888",   VTFImageFormat::RGB888,   false},
        {"BGR888",   VTFImageFormat::BGR888,   false},
        {"I8",       VTFImageFormat::I8,       false},
        {"IA88",     VTFImageFormat::IA88,     true},
        {"A8",       VTFImageFormat::A8,       true},
    };
    for (const Case& c : cases) {
        // Two mips: the 1x1 one first, in a colour that is none of the four, then the
        // 2x2 one. Reading the 1x1 by mistake shows as wrong pixels.
        auto file = header(c.format, 2, 2, 1, 2);
        encode(file, c.format, {1, 2, 3, 4});
        for (const Rgba& p : kPixels) encode(file, c.format, p);

        const auto res = VTFParser::parse(file);
        if (!res) {
            std::printf("  FAIL %s refused (error %d)\n", c.name, static_cast<int>(res.error()));
            ++failures;
            continue;
        }
        check((std::string(c.name) + " width").c_str(), static_cast<int>(res->width), 2);
        check((std::string(c.name) + " height").c_str(), static_cast<int>(res->height), 2);
        check((std::string(c.name) + " has_alpha").c_str(), res->has_alpha, c.alpha);
        check_pixels(c.name, *res, c.format);
    }

    // ---- frame 0 of an animated texture ----------------------------------------
    {
        // Order is mip, then frame: [1x1 f0][1x1 f1][2x2 f0][2x2 f1].
        const auto f = VTFImageFormat::RGBA8888;
        auto file = header(f, 2, 2, 2, 2);
        encode(file, f, {1, 1, 1, 1});
        encode(file, f, {2, 2, 2, 2});
        for (const Rgba& p : kPixels) encode(file, f, p);
        for (int i = 0; i < 4; ++i) encode(file, f, {200, 200, 200, 200});

        const auto res = VTFParser::parse(file);
        if (!res) fail("two-frame texture refused");
        else check_pixels("frame 0", *res, f);
    }

    // ---- DXT1 through the whole reader -----------------------------------------
    {
        // One 4x4 block, both end colours pure red, every index 0.
        auto file = header(VTFImageFormat::DXT1, 4, 4, 1, 1);
        const uint8_t block[8] = {0x00, 0xF8, 0x00, 0xF8, 0, 0, 0, 0};
        put(file, block, sizeof(block));
        const auto res = VTFParser::parse(file);
        if (!res) fail("DXT1 refused");
        else {
            check("DXT1 px0 r", res->rgba8_pixels[0], 255);
            check("DXT1 px0 g", res->rgba8_pixels[1], 0);
            check("DXT1 px15 b", res->rgba8_pixels[15 * 4 + 2], 0);
            check("DXT1 has_alpha", res->has_alpha, false);
        }
    }

    // ---- DXT3 through the whole reader -----------------------------------------
    {
        // One block: every alpha nibble 8 (136), then a red colour block.
        auto file = header(VTFImageFormat::DXT3, 4, 4, 1, 1);
        push_n(file, 8, 0x88);
        const uint8_t color[8] = {0x00, 0xF8, 0x00, 0xF8, 0, 0, 0, 0};
        put(file, color, sizeof(color));
        const auto res = VTFParser::parse(file);
        if (!res) fail("DXT3 refused");
        else {
            check("DXT3 px0 r", res->rgba8_pixels[0], 255);
            check("DXT3 px0 a", res->rgba8_pixels[3], 136);
            check("DXT3 px15 a", res->rgba8_pixels[15 * 4 + 3], 136);
            check("DXT3 has_alpha", res->has_alpha, true);
        }
    }

    // ---- DXT1 with one-bit alpha has the number Valve gives it ------------------
    {
        check("DXT1_OneBitAlpha is 20", static_cast<int>(VTFImageFormat::DXT1_OneBitAlpha), 20);
        auto file = header(VTFImageFormat::DXT1_OneBitAlpha, 4, 4, 1, 1);
        const uint8_t block[8] = {0x00, 0xF8, 0x00, 0xF8, 0, 0, 0, 0};
        put(file, block, sizeof(block));
        const auto res = VTFParser::parse(file);
        if (!res) fail("DXT1_OneBitAlpha refused");
        else check("DXT1_OneBitAlpha px0 r", res->rgba8_pixels[0], 255);
    }

    // ---- 7.2 with a thumbnail before the image ----------------------------------
    {
        // A 4x4 DXT1 thumbnail (8 bytes) sits between the header and the mips.
        const auto f = VTFImageFormat::RGBA8888;
        auto file = header(f, 2, 2, 1, 1);
        VTFHeader* hd = reinterpret_cast<VTFHeader*>(file.data());
        hd->low_res_image_format = static_cast<uint32_t>(VTFImageFormat::DXT1);
        hd->low_res_image_width = 4;
        hd->low_res_image_height = 4;
        push_n(file, 8, 0xEE);
        for (const Rgba& p : kPixels) encode(file, f, p);
        for (int i = 0; i < 4; ++i) encode(file, f, {200, 200, 200, 200}); // trailing
        const auto res = VTFParser::parse(file);
        if (!res) fail("7.2 with thumbnail refused");
        else check_pixels("7.2 thumbnail", *res, f);
    }

    // ---- 7.3: the resource list says where the image is -------------------------
    {
        // Three entries: a CRC whose value sits in the entry itself, the thumbnail, the
        // image. Bytes follow the image, so the image is not where the file ends.
        const auto f = VTFImageFormat::RGBA8888;
        auto file = header(f, 2, 2, 1, 1, 104);
        poke32(file, kMinorAt, 3);
        poke32(file, kResourceCountAt, 3);
        const uint8_t crc[8] = {'C', 'R', 'C', 0x2, 0x12, 0x34, 0x56, 0x78};
        std::memcpy(file.data() + 80, crc, 8);
        const uint8_t thumb[4] = {0x01, 0, 0, 0};
        std::memcpy(file.data() + 88, thumb, 4);
        poke32(file, 92, 104);
        const uint8_t image[4] = {0x30, 0, 0, 0};
        std::memcpy(file.data() + 96, image, 4);
        poke32(file, 100, 112);
        VTFHeader* hd = reinterpret_cast<VTFHeader*>(file.data());
        hd->low_res_image_format = static_cast<uint32_t>(VTFImageFormat::DXT1);
        hd->low_res_image_width = 4;
        hd->low_res_image_height = 4;
        push_n(file, 8, 0xEE);  // thumbnail at 104
        for (const Rgba& p : kPixels) encode(file, f, p);  // image at 112
        push_n(file, 32, 0xDD);  // something after the image
        const auto res = VTFParser::parse(file);
        if (!res) fail("7.3 with resources refused");
        else check_pixels("7.3 resources", *res, f);
    }

    // ---- cube maps: face 0 of the full-size mip ----------------------------------
    {
        // [1x1 x every face][2x2 face 0][2x2 the other faces]. Before 7.5 there is a
        // seventh face unless the first frame is -1; the 96 cube maps of a real 7.4 map
        // all have first frame 0 and seven faces. Guessing the count wrong reads one of
        // the 1x1 decoys into face 0.
        struct Cube { const char* name; uint32_t minor; uint16_t first_frame; int faces; };
        const Cube cubes[] = {
            {"7.2 cube, first frame -1", 2, 0xFFFF, 6},
            {"7.2 cube, first frame 0", 2, 0, 7},
            {"7.4 cube, first frame 0", 4, 0, 7},
            {"7.5 cube, first frame 0", 5, 0, 6},
        };
        const auto f = VTFImageFormat::RGBA8888;
        for (const Cube& c : cubes) {
            const bool resources = c.minor >= 3;
            auto file = header(f, 2, 2, 1, 2, resources ? 88 : 80);
            poke32(file, kMinorAt, c.minor);
            poke32(file, kFlagsAt, kVtfFlagEnvmap);
            poke16(file, kFirstFrameAt, c.first_frame);
            if (resources) {
                poke32(file, kResourceCountAt, 1);
                const uint8_t image[4] = {0x30, 0, 0, 0};
                std::memcpy(file.data() + 80, image, 4);
                poke32(file, 84, 88);
            }
            for (int face = 0; face < c.faces; ++face) encode(file, f, {1, 2, 3, 4});
            for (const Rgba& p : kPixels) encode(file, f, p);
            for (int i = 0; i < 4 * (c.faces - 1); ++i) encode(file, f, {200, 200, 200, 200});
            const auto res = VTFParser::parse(file);
            if (!res) {
                std::printf("  FAIL %s refused\n", c.name);
                ++failures;
            } else {
                check_pixels(c.name, *res, f);
            }
        }
    }

    // ---- refusals ----------------------------------------------------------------
    {
        auto file = header(VTFImageFormat::RGBA8888, 2, 2, 1, 1);
        file[0] = std::byte{'X'};
        for (const Rgba& p : kPixels) encode(file, VTFImageFormat::RGBA8888, p);
        const auto res = VTFParser::parse(file);
        check("bad magic refused", res.has_value(), false);
    }
    {
        // Header says 64x64 RGBA (16 KB) and only four pixels follow.
        auto file = header(VTFImageFormat::RGBA8888, 64, 64, 1, 1);
        for (const Rgba& p : kPixels) encode(file, VTFImageFormat::RGBA8888, p);
        const auto res = VTFParser::parse(file);
        check("truncated refused", res.has_value(), false);
    }
    {
        // A format with no decoder must fail, not come back as a picture of noise.
        auto file = header(VTFImageFormat::P8, 2, 2, 1, 1);
        for (int i = 0; i < 4; ++i) file.push_back(std::byte{7});
        const auto res = VTFParser::parse(file);
        check("P8 refused", res.has_value(), false);
    }
    {
        const std::vector<std::byte> empty;
        check("empty refused", VTFParser::parse(empty).has_value(), false);
    }

    if (failures == 0) std::printf("  all checks passed\n");
    return failures == 0 ? 0 : 1;
}
