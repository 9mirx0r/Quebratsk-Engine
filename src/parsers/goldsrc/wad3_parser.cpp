#include "wad3_parser.h"
#include <cstring>

namespace quebratsk::parsers::goldsrc {

std::expected<ir::IRTextureData, WAD3ParseError> WAD3Parser::parse_miptex(
    std::span<const std::byte> miptex_bytes
) {
    if (miptex_bytes.size() < sizeof(MiptexHeader)) {
        return std::unexpected(WAD3ParseError::InvalidMiptex);
    }

    auto* mip_hdr = reinterpret_cast<const MiptexHeader*>(miptex_bytes.data());
    ir::IRTextureData result;
    result.name = std::string(mip_hdr->name, strnlen(mip_hdr->name, sizeof(mip_hdr->name)));
    result.width = mip_hdr->width;
    result.height = mip_hdr->height;

    // GoldSrc requires both dimensions to be multiples of 16; the mip-chain size
    // arithmetic below (num_pixels/4, /16, /64) is only exact under that assumption.
    if (result.width == 0 || result.height == 0 ||
        result.width % 16 != 0 || result.height % 16 != 0 ||
        result.width > 4096 || result.height > 4096) {
        return std::unexpected(WAD3ParseError::InvalidMiptex);
    }

    size_t num_pixels = static_cast<size_t>(result.width) * static_cast<size_t>(result.height);
    size_t mip0_offset = static_cast<size_t>(mip_hdr->offsets[0]);

    if (mip0_offset + num_pixels > miptex_bytes.size()) {
        return std::unexpected(WAD3ParseError::CorruptedData);
    }

    // Palette is located after all 4 mipmap levels + 2-byte palette size count (256)
    // Mip0 = w*h, Mip1 = w/2*h/2, Mip2 = w/4*h/4, Mip3 = w/8*h/8
    size_t all_mips_size = num_pixels + (num_pixels / 4) + (num_pixels / 16) + (num_pixels / 64);
    size_t palette_offset = mip0_offset + all_mips_size + sizeof(uint16_t);

    if (palette_offset + 768 > miptex_bytes.size()) {
        return std::unexpected(WAD3ParseError::CorruptedData);
    }

    const uint8_t* indices = reinterpret_cast<const uint8_t*>(miptex_bytes.data() + mip0_offset);
    const uint8_t* palette = reinterpret_cast<const uint8_t*>(miptex_bytes.data() + palette_offset);

    // GoldSrc convention: textures whose name starts with '{' use palette index 255
    // as a transparency key.
    const bool is_masked = result.name.starts_with("{");
    result.has_alpha = is_masked;

    result.rgba8_pixels.resize(num_pixels * 4);

    for (size_t i = 0; i < num_pixels; ++i) {
        uint8_t idx = indices[i];
        result.rgba8_pixels[i * 4 + 0] = palette[idx * 3 + 0]; // R
        result.rgba8_pixels[i * 4 + 1] = palette[idx * 3 + 1]; // G
        result.rgba8_pixels[i * 4 + 2] = palette[idx * 3 + 2]; // B
        result.rgba8_pixels[i * 4 + 3] = (is_masked && idx == 255) ? 0 : 255;
    }

    return result;
}

} // namespace quebratsk::parsers::goldsrc
