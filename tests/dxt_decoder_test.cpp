// Standalone verification of the quebratsk::image DXT decoders.
//
// dxt_decoder.cpp has no Godot dependency, so it compiles and runs on its own —
// no engine, no godot-cpp, no test framework required.
//
// Build and run (from the repository root, in a Developer Command Prompt):
//   cl /nologo /std:c++20 /EHsc /I src/core/image ^
//      tests/dxt_decoder_test.cpp src/core/image/dxt_decoder.cpp /Fe:dxt_test.exe
//   dxt_test.exe
//
// Or with GCC/Clang:
//   c++ -std=c++20 -I src/core/image tests/dxt_decoder_test.cpp \
//       src/core/image/dxt_decoder.cpp -o dxt_test && ./dxt_test
//
// Exits non-zero on failure. Covers both DXT1 colour modes, DXT5 interpolated
// alpha, blocks clipped by non-multiple-of-4 dimensions, and rejection of
// truncated input.
#include "dxt_decoder.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace quebratsk::image;

static int failures = 0;

static void check(const char* what, int got, int want) {
    if (got != want) {
        std::printf("  FAIL %-40s got %3d want %3d\n", what, got, want);
        ++failures;
    }
}

static void put16(std::vector<std::byte>& b, uint16_t v) {
    b.push_back(std::byte(v & 0xFF));
    b.push_back(std::byte((v >> 8) & 0xFF));
}
static void put32(std::vector<std::byte>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(std::byte((v >> (8 * i)) & 0xFF));
}

int main() {
    std::printf("DXT decoder verification\n\n");

    // ---- DXT1, 4-colour mode (c0 > c1) ----------------------------------------
    // c0 = 0xF800 -> pure red (255,0,0); c1 = 0x001F -> pure blue (0,0,255)
    // Index layout: 2 bits per pixel, pixel 0 is the low bits.
    // Choose indices 0,1,2,3 for the first four pixels: 0b11100100 = 0xE4
    {
        std::vector<std::byte> block;
        put16(block, 0xF800);      // c0 red
        put16(block, 0x001F);      // c1 blue
        put32(block, 0x000000E4);  // px0=0, px1=1, px2=2, px3=3, rest 0

        std::vector<uint8_t> out;
        if (!decode_dxt1(block, 4, 4, out)) {
            std::printf("  FAIL decode_dxt1 returned false\n");
            ++failures;
        } else {
            std::printf("DXT1 4-colour mode:\n");
            // px0 = palette[0] = red
            check("px0.r", out[0], 255);
            check("px0.g", out[1], 0);
            check("px0.b", out[2], 0);
            check("px0.a", out[3], 255);
            // px1 = palette[1] = blue
            check("px1.r", out[4], 0);
            check("px1.b", out[6], 255);
            // px2 = (2*c0 + c1)/3
            check("px2.r", out[8], (2 * 255 + 0) / 3);   // 170
            check("px2.b", out[10], (2 * 0 + 255) / 3);  // 85
            // px3 = (2*c1 + c0)/3
            check("px3.r", out[12], (2 * 0 + 255) / 3);  // 85
            check("px3.b", out[14], (2 * 255 + 0) / 3);  // 170
            check("size", (int)out.size(), 4 * 4 * 4);
        }
    }

    // ---- DXT1 punch-through mode (c0 <= c1): index 3 is transparent ------------
    {
        std::vector<std::byte> block;
        put16(block, 0x001F);      // c0 blue  (smaller)
        put16(block, 0xF800);      // c1 red   (larger)  -> c0 <= c1
        put32(block, 0x000000E4);  // px0=0 px1=1 px2=2 px3=3

        std::vector<uint8_t> out;
        if (!decode_dxt1(block, 4, 4, out)) {
            std::printf("  FAIL decode_dxt1 returned false\n");
            ++failures;
            return 1;
        }
        std::printf("DXT1 punch-through mode:\n");
        check("px2.r (midpoint)", out[8], (0 + 255) / 2);   // 127
        check("px3.a (transparent)", out[15], 0);
        check("px0.a (opaque)", out[3], 255);
    }

    // ---- DXT5 alpha block, 8-value mode (a0 > a1) -----------------------------
    {
        std::vector<std::byte> block;
        block.push_back(std::byte(255)); // a0
        block.push_back(std::byte(0));   // a1  -> a0 > a1, 8-value table
        // 3 bits per pixel. px0=0 (=a0=255), px1=1 (=a1=0), px2=2 (=first interp).
        // bits = 0 | (1<<3) | (2<<6) = 0b010001000 = 0x88
        block.push_back(std::byte(0x88));
        for (int i = 0; i < 5; ++i) block.push_back(std::byte(0));
        // colour half: solid red, all indices 0
        put16(block, 0xF800);
        put16(block, 0x001F);
        put32(block, 0x00000000);

        std::vector<uint8_t> out;
        if (!decode_dxt5(block, 4, 4, out)) {
            std::printf("  FAIL decode_dxt5 returned false\n");
            ++failures;
        } else {
            std::printf("DXT5 alpha (8-value mode):\n");
            check("px0.a", out[3], 255);
            check("px1.a", out[7], 0);
            check("px2.a", out[11], (6 * 255 + 1 * 0) / 7); // 218
            check("px0.r (colour intact)", out[0], 255);
        }
    }

    // ---- Non-multiple-of-4 dimensions must not read or write out of bounds ----
    {
        // 3x2 image still occupies one 4x4 block on disk.
        std::vector<std::byte> block;
        put16(block, 0xF800);
        put16(block, 0x001F);
        put32(block, 0x00000000);

        std::vector<uint8_t> out;
        std::printf("Clipped block (3x2):\n");
        if (!decode_dxt1(block, 3, 2, out)) { // NOLINT: return value is checked
            std::printf("  FAIL decode_dxt1(3,2) returned false\n");
            ++failures;
        }
        check("size", (int)out.size(), 3 * 2 * 4);
        check("last px r", out[(3 * 2 - 1) * 4 + 0], 255);
    }

    // ---- Truncated input must be rejected, not read past the end --------------
    {
        std::vector<std::byte> tiny(4, std::byte(0)); // half a block
        std::vector<uint8_t> out;
        std::printf("Truncated input:\n");
        if (decode_dxt1(tiny, 4, 4, out)) {
            std::printf("  FAIL decode_dxt1 accepted a truncated block\n");
            ++failures;
        }
        if (decode_dxt5(tiny, 4, 4, out)) {
            std::printf("  FAIL decode_dxt5 accepted a truncated block\n");
            ++failures;
        }
        check("size accounting dxt1", (int)dxt1_encoded_size(8, 8), 4 * 8);
        check("size accounting dxt5", (int)dxt5_encoded_size(8, 8), 4 * 16);
        check("size accounting non-mult4", (int)dxt1_encoded_size(5, 5), 4 * 8);
    }

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASS" : "FAILURES",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
