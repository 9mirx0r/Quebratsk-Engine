#include "spr_parser.h"
#include <cstring>

namespace quebratsk::parsers::goldsrc {

std::expected<ParsedSprite, SPRParseError> SPRParser::parse(
    std::span<const std::byte> spr_bytes
) {
    if (spr_bytes.size() < sizeof(SpriteHeader)) {
        return std::unexpected(SPRParseError::InvalidHeader);
    }

    auto* header = reinterpret_cast<const SpriteHeader*>(spr_bytes.data());
    if (std::memcmp(header->magic, kSprMagic.data(), 4) != 0 || header->version != kSprVersion) {
        return std::unexpected(SPRParseError::VersionMismatch);
    }

    ParsedSprite sprite;
    sprite.type = header->type;
    sprite.max_width = header->max_width;
    sprite.max_height = header->max_height;

    // Palette: starts after 2-byte palette size count (256)
    size_t cursor = sizeof(SpriteHeader);
    if (cursor + sizeof(uint16_t) + 768 > spr_bytes.size()) {
        return std::unexpected(SPRParseError::CorruptedData);
    }

    uint16_t palette_size = 0;
    std::memcpy(&palette_size, spr_bytes.data() + cursor, sizeof(uint16_t));
    cursor += sizeof(uint16_t);

    const uint8_t* palette = reinterpret_cast<const uint8_t*>(spr_bytes.data() + cursor);
    cursor += 768; // 256 RGB colors

    for (int32_t i = 0; i < header->num_frames && cursor < spr_bytes.size(); ++i) {
        if (cursor + sizeof(SpriteFrameHeader) > spr_bytes.size()) break;

        auto* frame_hdr = reinterpret_cast<const SpriteFrameHeader*>(spr_bytes.data() + cursor);
        cursor += sizeof(SpriteFrameHeader);

        DecodedSpriteFrame frame;
        frame.origin_x = frame_hdr->origin_x;
        frame.origin_y = frame_hdr->origin_y;
        frame.width = frame_hdr->width;
        frame.height = frame_hdr->height;

        size_t num_pixels = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height);
        if (cursor + num_pixels > spr_bytes.size()) break;

        const uint8_t* indices = reinterpret_cast<const uint8_t*>(spr_bytes.data() + cursor);
        cursor += num_pixels;

        frame.rgba8_pixels.resize(num_pixels * 4);
        for (size_t p = 0; p < num_pixels; ++p) {
            uint8_t idx = indices[p];
            frame.rgba8_pixels[p * 4 + 0] = palette[idx * 3 + 0]; // R
            frame.rgba8_pixels[p * 4 + 1] = palette[idx * 3 + 1]; // G
            frame.rgba8_pixels[p * 4 + 2] = palette[idx * 3 + 2]; // B
            frame.rgba8_pixels[p * 4 + 3] = (idx == 255) ? 0 : 255; // 255 = Transparent index
        }

        sprite.frames.push_back(std::move(frame));
    }

    return sprite;
}

} // namespace quebratsk::parsers::goldsrc
