#include "dxt_decoder.h"

#include <cstring>

namespace quebratsk::image {

namespace {

constexpr size_t kBlockDim = 4;

struct Rgba {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

size_t blocks_across(uint32_t dim) noexcept {
    return (static_cast<size_t>(dim) + kBlockDim - 1) / kBlockDim;
}

uint16_t read_u16(const std::byte* p) noexcept {
    return static_cast<uint16_t>(static_cast<uint8_t>(p[0])) |
           static_cast<uint16_t>(static_cast<uint16_t>(static_cast<uint8_t>(p[1])) << 8);
}

uint32_t read_u32(const std::byte* p) noexcept {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v |= static_cast<uint32_t>(static_cast<uint8_t>(p[i])) << (8 * i);
    }
    return v;
}

/// Expand RGB565 to RGB888 by replicating the high bits into the low ones, so that
/// an all-ones source maps to 255 rather than 248.
Rgba unpack_565(uint16_t c) noexcept {
    const uint8_t r5 = static_cast<uint8_t>((c >> 11) & 0x1F);
    const uint8_t g6 = static_cast<uint8_t>((c >> 5) & 0x3F);
    const uint8_t b5 = static_cast<uint8_t>(c & 0x1F);

    Rgba out;
    out.r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
    out.g = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
    out.b = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
    out.a = 255;
    return out;
}

uint8_t lerp_third(uint8_t a, uint8_t b) noexcept {
    return static_cast<uint8_t>((2 * static_cast<int>(a) + static_cast<int>(b)) / 3);
}

/// Decode the 8-byte colour half of a DXT block into a 4-entry palette.
/// `punchthrough` selects DXT1's 1-bit-alpha mode; DXT3/DXT5 always use 4 colours.
void build_color_palette(const std::byte* block, bool punchthrough, Rgba (&palette)[4]) noexcept {
    const uint16_t c0 = read_u16(block);
    const uint16_t c1 = read_u16(block + 2);

    palette[0] = unpack_565(c0);
    palette[1] = unpack_565(c1);

    if (!punchthrough || c0 > c1) {
        palette[2].r = lerp_third(palette[0].r, palette[1].r);
        palette[2].g = lerp_third(palette[0].g, palette[1].g);
        palette[2].b = lerp_third(palette[0].b, palette[1].b);
        palette[2].a = 255;

        palette[3].r = lerp_third(palette[1].r, palette[0].r);
        palette[3].g = lerp_third(palette[1].g, palette[0].g);
        palette[3].b = lerp_third(palette[1].b, palette[0].b);
        palette[3].a = 255;
    } else {
        palette[2].r = static_cast<uint8_t>((static_cast<int>(palette[0].r) + palette[1].r) / 2);
        palette[2].g = static_cast<uint8_t>((static_cast<int>(palette[0].g) + palette[1].g) / 2);
        palette[2].b = static_cast<uint8_t>((static_cast<int>(palette[0].b) + palette[1].b) / 2);
        palette[2].a = 255;

        palette[3] = Rgba{0, 0, 0, 0}; // transparent black
    }
}

/// Write one 4x4 colour block, clipping against the declared image bounds.
void emit_color_block(const std::byte* block, const Rgba (&palette)[4],
                      uint32_t width, uint32_t height,
                      size_t block_x, size_t block_y,
                      std::vector<uint8_t>& out, const uint8_t* alpha_override) noexcept {
    const uint32_t indices = read_u32(block + 4);

    for (size_t py = 0; py < kBlockDim; ++py) {
        const size_t y = block_y * kBlockDim + py;
        if (y >= height) break;

        for (size_t px = 0; px < kBlockDim; ++px) {
            const size_t x = block_x * kBlockDim + px;
            if (x >= width) continue;

            const uint32_t shift = static_cast<uint32_t>(2 * (py * kBlockDim + px));
            const Rgba& c = palette[(indices >> shift) & 0x3];

            const size_t o = (y * static_cast<size_t>(width) + x) * 4;
            out[o + 0] = c.r;
            out[o + 1] = c.g;
            out[o + 2] = c.b;
            out[o + 3] = alpha_override ? alpha_override[py * kBlockDim + px] : c.a;
        }
    }
}

/// Expand the 8-byte DXT5 alpha block into 16 per-pixel values.
void decode_alpha_block(const std::byte* block, uint8_t (&out_alpha)[16]) noexcept {
    const uint8_t a0 = static_cast<uint8_t>(block[0]);
    const uint8_t a1 = static_cast<uint8_t>(block[1]);

    uint8_t table[8];
    table[0] = a0;
    table[1] = a1;
    if (a0 > a1) {
        for (int i = 1; i <= 6; ++i) {
            table[i + 1] = static_cast<uint8_t>(((7 - i) * a0 + i * a1) / 7);
        }
    } else {
        for (int i = 1; i <= 4; ++i) {
            table[i + 1] = static_cast<uint8_t>(((5 - i) * a0 + i * a1) / 5);
        }
        table[6] = 0;
        table[7] = 255;
    }

    // 16 pixels x 3 bits = 48 bits packed into the remaining 6 bytes.
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) {
        bits |= static_cast<uint64_t>(static_cast<uint8_t>(block[2 + i])) << (8 * i);
    }
    for (int i = 0; i < 16; ++i) {
        out_alpha[i] = table[(bits >> (3 * i)) & 0x7];
    }
}

} // namespace

size_t dxt1_encoded_size(uint32_t width, uint32_t height) noexcept {
    return blocks_across(width) * blocks_across(height) * 8;
}

size_t dxt5_encoded_size(uint32_t width, uint32_t height) noexcept {
    return blocks_across(width) * blocks_across(height) * 16;
}

bool decode_dxt1(std::span<const std::byte> src, uint32_t width, uint32_t height,
                 std::vector<uint8_t>& out_rgba8) {
    if (width == 0 || height == 0) return false;
    if (src.size() < dxt1_encoded_size(width, height)) return false;

    out_rgba8.assign(static_cast<size_t>(width) * height * 4, 0);

    const size_t bw = blocks_across(width);
    const size_t bh = blocks_across(height);

    for (size_t by = 0; by < bh; ++by) {
        for (size_t bx = 0; bx < bw; ++bx) {
            const std::byte* block = src.data() + (by * bw + bx) * 8;
            Rgba palette[4];
            build_color_palette(block, /*punchthrough=*/true, palette);
            emit_color_block(block, palette, width, height, bx, by, out_rgba8, nullptr);
        }
    }
    return true;
}

bool decode_dxt5(std::span<const std::byte> src, uint32_t width, uint32_t height,
                 std::vector<uint8_t>& out_rgba8) {
    if (width == 0 || height == 0) return false;
    if (src.size() < dxt5_encoded_size(width, height)) return false;

    out_rgba8.assign(static_cast<size_t>(width) * height * 4, 0);

    const size_t bw = blocks_across(width);
    const size_t bh = blocks_across(height);

    for (size_t by = 0; by < bh; ++by) {
        for (size_t bx = 0; bx < bw; ++bx) {
            const std::byte* block = src.data() + (by * bw + bx) * 16;

            uint8_t alpha[16];
            decode_alpha_block(block, alpha);

            Rgba palette[4];
            // DXT5 colour blocks never use the punch-through mode.
            build_color_palette(block + 8, /*punchthrough=*/false, palette);
            emit_color_block(block + 8, palette, width, height, bx, by, out_rgba8, alpha);
        }
    }
    return true;
}

} // namespace quebratsk::image
