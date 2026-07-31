// Standalone verification of the Real Virtuality PAA container reader.
//
// What this can and cannot tell you: it builds PAA files byte by byte and checks that the
// reader walks them the way the format says it should. It cannot confirm that the format
// says what this test assumes, because the test and the parser were written from the same
// reading of it. Only a retail Arma or DayZ install settles that, and until one has been
// read, .paa is not listed as supported anywhere in this project.
//
// What it does settle is the part that has bitten this codebase before: that a truncated,
// lying or hostile file is refused rather than walked off the end of the buffer.
//
// Build and run (from the repository root):
//   c++ -std=c++23 -I src tests/paa_parser_test.cpp \
//       src/parsers/rv_enfusion/paa_parser.cpp src/core/image/dxt_decoder.cpp -o paa_test
#include "../src/parsers/rv_enfusion/paa_parser.h"

#include <cstdio>
#include <vector>

using namespace quebratsk::parsers::rv_enfusion;
namespace ir = quebratsk::ir;

static int failures = 0;

static void check(const char* what, long long got, long long want) {
    if (got != want) {
        std::printf("  FAIL %-46s got %6lld want %6lld\n", what, got, want);
        ++failures;
    }
}

static void check_err(const char* what, const std::expected<ir::IRTextureData, PAAParseError>& r,
                      PAAParseError want) {
    if (r.has_value()) {
        std::printf("  FAIL %-46s parsed when it should not have\n", what);
        ++failures;
    } else if (r.error() != want) {
        std::printf("  FAIL %-46s wrong error\n", what);
        ++failures;
    }
}

// ------------------------------------------------------------------ builders ----

static void put8(std::vector<std::byte>& b, uint8_t v) { b.push_back(std::byte(v)); }
static void put16(std::vector<std::byte>& b, uint16_t v) {
    put8(b, v & 0xFF);
    put8(b, (v >> 8) & 0xFF);
}
static void put24(std::vector<std::byte>& b, uint32_t v) {
    put8(b, v & 0xFF);
    put8(b, (v >> 8) & 0xFF);
    put8(b, (v >> 16) & 0xFF);
}
static void put32(std::vector<std::byte>& b, uint32_t v) {
    put16(b, v & 0xFFFF);
    put16(b, (v >> 16) & 0xFFFF);
}
static void put_tag(std::vector<std::byte>& b, const char* name, uint32_t length) {
    for (const char* p = "GGAT"; *p; ++p) put8(b, static_cast<uint8_t>(*p));
    for (int i = 0; i < 4; ++i) put8(b, static_cast<uint8_t>(name[i]));
    put32(b, length);
    for (uint32_t i = 0; i < length; ++i) put8(b, 0xAB);
}

/// A PAA carrying one mipmap of `pixels`, with the tag chain and palette a real file has.
static std::vector<std::byte> make_paa(uint16_t type, uint16_t w, uint16_t h,
                                       const std::vector<std::byte>& pixels,
                                       bool compressed = false,
                                       uint16_t palette_entries = 0) {
    std::vector<std::byte> f;
    put16(f, type);
    put_tag(f, "SFFO", 64);   // OFFS, the mipmap offset table
    put_tag(f, "CGVA", 4);    // AVGC, average colour
    put16(f, palette_entries);
    for (uint16_t i = 0; i < palette_entries; ++i) {
        put8(f, 1); put8(f, 2); put8(f, 3);
    }
    put16(f, static_cast<uint16_t>(compressed ? (w | 0x8000) : w));
    put16(f, h);
    put24(f, static_cast<uint32_t>(pixels.size()));
    f.insert(f.end(), pixels.begin(), pixels.end());
    put16(f, 0);  // terminator
    put16(f, 0);
    return f;
}

int main() {
    std::printf("PAA container verification\n\n");

    // ---- a 4x4 DXT1 block, the smallest real texture -------------------------------
    {
        std::vector<std::byte> block;
        put16(block, 0xFFFF);  // colour0: white
        put16(block, 0x0000);  // colour1: black
        put32(block, 0x00000000);  // every pixel takes colour0

        const auto file = make_paa(0xFF01, 4, 4, block);
        auto info = PAAParser::parse_info(file);
        if (!info.has_value()) {
            std::printf("  FAIL DXT1 header did not parse\n");
            ++failures;
        } else {
            check("DXT1 format word", static_cast<int>(info->format), 0xFF01);
            check("DXT1 mipmap count", static_cast<long long>(info->mipmaps.size()), 1);
            check("DXT1 width", info->mipmaps[0].width, 4);
            check("DXT1 height", info->mipmaps[0].height, 4);
            check("DXT1 not flagged compressed", info->mipmaps[0].compressed ? 1 : 0, 0);
        }

        auto tex = PAAParser::parse(file);
        if (!tex.has_value()) {
            std::printf("  FAIL DXT1 pixels did not decode\n");
            ++failures;
        } else {
            check("DXT1 pixel buffer", static_cast<long long>(tex->rgba8_pixels.size()), 4 * 4 * 4);
            check("DXT1 first pixel red", tex->rgba8_pixels[0], 255);
            check("DXT1 first pixel green", tex->rgba8_pixels[1], 255);
            check("DXT1 first pixel blue", tex->rgba8_pixels[2], 255);
        }
    }

    // ---- the packed layouts, where channel order is the easy thing to get wrong ----
    {
        std::vector<std::byte> px;
        put8(px, 0x40);  // B
        put8(px, 0x80);  // G
        put8(px, 0xC0);  // R
        put8(px, 0xFF);  // A
        auto tex = PAAParser::parse(make_paa(0x8888, 1, 1, px));
        if (!tex.has_value()) {
            std::printf("  FAIL ARGB8888 did not decode\n");
            ++failures;
        } else {
            check("ARGB8888 red", tex->rgba8_pixels[0], 0xC0);
            check("ARGB8888 green", tex->rgba8_pixels[1], 0x80);
            check("ARGB8888 blue", tex->rgba8_pixels[2], 0x40);
            check("ARGB8888 alpha", tex->rgba8_pixels[3], 0xFF);
        }
    }
    {
        std::vector<std::byte> px;
        put16(px, 0xFF00);  // a=F r=F g=0 b=0
        auto tex = PAAParser::parse(make_paa(0x4444, 1, 1, px));
        if (!tex.has_value()) {
            std::printf("  FAIL ARGB4444 did not decode\n");
            ++failures;
        } else {
            // 0xF over four bits must reach a full 255, not 240: shifting alone would
            // leave every white texture slightly grey.
            check("ARGB4444 red saturates", tex->rgba8_pixels[0], 255);
            check("ARGB4444 green", tex->rgba8_pixels[1], 0);
            check("ARGB4444 alpha saturates", tex->rgba8_pixels[3], 255);
        }
    }
    {
        std::vector<std::byte> px;
        put8(px, 0x77);  // intensity
        put8(px, 0x33);  // alpha
        auto tex = PAAParser::parse(make_paa(0x8080, 1, 1, px));
        if (!tex.has_value()) {
            std::printf("  FAIL AI88 did not decode\n");
            ++failures;
        } else {
            check("AI88 intensity to red", tex->rgba8_pixels[0], 0x77);
            check("AI88 intensity to blue", tex->rgba8_pixels[2], 0x77);
            check("AI88 alpha", tex->rgba8_pixels[3], 0x33);
        }
    }

    // ---- a palette must be stepped over, or every mipmap reads from the wrong place --
    {
        std::vector<std::byte> block;
        put16(block, 0xFFFF);
        put16(block, 0x0000);
        put32(block, 0x00000000);
        auto info = PAAParser::parse_info(make_paa(0xFF01, 4, 4, block, false, 17));
        if (!info.has_value()) {
            std::printf("  FAIL palette file did not parse\n");
            ++failures;
        } else {
            check("mipmap found past a 17-entry palette", info->mipmaps[0].width, 4);
        }
    }

    // ---- refusals ------------------------------------------------------------------
    {
        std::vector<std::byte> block(8, std::byte{0});
        check_err("compressed mipmap is refused, not guessed",
                  PAAParser::parse(make_paa(0xFF01, 4, 4, block, true)),
                  PAAParseError::CompressedMipmap);

        std::vector<std::byte> not_paa{std::byte{'B'}, std::byte{'M'}, std::byte{0}, std::byte{0}};
        check_err("a file that is not a PAA", PAAParser::parse(not_paa),
                  PAAParseError::UnknownFormat);

        check_err("an empty buffer", PAAParser::parse({}), PAAParseError::Truncated);

        // A length field larger than the file. The reader must refuse rather than hand
        // back a span reaching past the mapping, which is how this codebase's earlier
        // out-of-bounds reads happened.
        std::vector<std::byte> lying;
        put16(lying, 0xFF01);
        put16(lying, 0);           // no tags, no palette
        put16(lying, 4);
        put16(lying, 4);
        put24(lying, 0xFFFFFF);    // claims 16 MB of pixels
        check_err("a mipmap length past the end of the file", PAAParser::parse(lying),
                  PAAParseError::Truncated);

        // Every prefix of a valid file must be refused rather than read past the end.
        std::vector<std::byte> block2;
        put16(block2, 0xFFFF);
        put16(block2, 0x0000);
        put32(block2, 0x00000000);
        const auto whole = make_paa(0xFF01, 4, 4, block2);
        int survived = 0;
        for (size_t cut = 1; cut < whole.size(); ++cut) {
            std::vector<std::byte> partial(whole.begin(), whole.begin() + cut);
            auto r = PAAParser::parse(partial);
            if (r.has_value()) ++survived;
        }
        check("no truncation parses as a whole file", survived, 0);
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
