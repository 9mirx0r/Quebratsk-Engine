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
    sprite.tex_format = header->tex_format;
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

    // The 768-byte palette read below assumes 256 RGB triples.
    if (palette_size != 256) {
        return std::unexpected(SPRParseError::CorruptedData);
    }

    const uint8_t* palette = reinterpret_cast<const uint8_t*>(spr_bytes.data() + cursor);
    cursor += 768; // 256 RGB colors

    // Each top-level frame is introduced by dspriteframetype_t: an int32 that is
    // 0 for "one frame follows" or 1 for "a group follows". The retail corpus only
    // uses 0, but the type word is always present: reading the frame header without
    // skipping it shifts origin_y into width and decodes pixels from the wrong offset.
    //
    // A group is, per the public SDK writer (utils/sprgen/sprgen.c, WriteSprite):
    // an int32 member count, then one float per member (cumulative seconds), then
    // the member frames. No shipped sprite has a group, so this layout rests on the
    // SDK source alone and has never been exercised against a real file.
    constexpr int32_t kFrameTypeSingle = 0;
    constexpr int32_t kFrameTypeGroup = 1;
    constexpr int32_t kMaxGroupMembers = 64;
    // width/height are int32_t. A negative value casts to a huge size_t, the
    // multiplication wraps, and both the resize() below and the pixel loop then
    // run out of bounds. GoldSrc sprites never exceed 4096 px on a side.
    constexpr int32_t kMaxSpriteDim = 4096;

    const size_t size = spr_bytes.size();
    // Subtraction, not addition: cursor + n can wrap.
    auto fits = [&](size_t n) { return cursor <= size && n <= size - cursor; };
    auto read_i32 = [&](int32_t& out) {
        if (!fits(sizeof(int32_t))) return false;
        std::memcpy(&out, spr_bytes.data() + cursor, sizeof(int32_t));
        cursor += sizeof(int32_t);
        return true;
    };

    const auto format = static_cast<SpriteTexFormat>(header->tex_format);
    // Index-alpha sprites are a single colour (the last palette entry) whose
    // opacity is the pixel's palette index.
    const uint8_t* tint = palette + 255 * 3;

    std::vector<float> intervals;
    for (int32_t i = 0; i < header->num_frames && sprite.complete; ++i) {
        int32_t frame_type = 0;
        if (!read_i32(frame_type)) {
            sprite.complete = false;
            break;
        }

        int32_t members = 1;
        intervals.clear();
        if (frame_type == kFrameTypeGroup) {
            if (!read_i32(members) || members <= 0 || members > kMaxGroupMembers ||
                !fits(static_cast<size_t>(members) * sizeof(float))) {
                sprite.complete = false;
                break;
            }
            intervals.resize(static_cast<size_t>(members));
            std::memcpy(intervals.data(), spr_bytes.data() + cursor,
                        intervals.size() * sizeof(float));
            cursor += intervals.size() * sizeof(float);
        } else if (frame_type != kFrameTypeSingle) {
            // Unknown frame type: the rest of the stream cannot be delimited.
            sprite.complete = false;
            break;
        }

        for (int32_t member = 0; member < members; ++member) {
            if (!fits(sizeof(SpriteFrameHeader))) {
                sprite.complete = false;
                break;
            }

            SpriteFrameHeader frame_hdr;
            std::memcpy(&frame_hdr, spr_bytes.data() + cursor, sizeof(SpriteFrameHeader));
            cursor += sizeof(SpriteFrameHeader);

            if (frame_hdr.width <= 0 || frame_hdr.height <= 0 ||
                frame_hdr.width > kMaxSpriteDim || frame_hdr.height > kMaxSpriteDim) {
                sprite.complete = false;
                break;
            }

            DecodedSpriteFrame frame;
            frame.origin_x = frame_hdr.origin_x;
            frame.origin_y = frame_hdr.origin_y;
            frame.width = frame_hdr.width;
            frame.height = frame_hdr.height;
            if (!intervals.empty()) {
                frame.group_interval = intervals[static_cast<size_t>(member)];
            }

            const size_t num_pixels =
                static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height);
            if (!fits(num_pixels)) {
                sprite.complete = false;
                break;
            }

            const uint8_t* indices = reinterpret_cast<const uint8_t*>(spr_bytes.data() + cursor);
            cursor += num_pixels;

            // The file stores only palette indices; what index 255 means depends on
            // the blend mode. These rules are the engine's, known from open
            // reimplementations rather than measured here, so treat them as reference
            // knowledge. Measured: 223 of 241 alpha-test sprites use index 255, and so
            // do 457 of 945 additive ones, where it must stay opaque.
            frame.rgba8_pixels.resize(num_pixels * 4);
            for (size_t p = 0; p < num_pixels; ++p) {
                const uint8_t idx = indices[p];
                const uint8_t* rgb = (format == SpriteTexFormat::IndexAlpha) ? tint : palette + idx * 3;
                uint8_t alpha = 255;
                if (format == SpriteTexFormat::IndexAlpha) {
                    alpha = idx;
                } else if (format == SpriteTexFormat::AlphaTest && idx == 255) {
                    alpha = 0;
                }
                frame.rgba8_pixels[p * 4 + 0] = rgb[0];
                frame.rgba8_pixels[p * 4 + 1] = rgb[1];
                frame.rgba8_pixels[p * 4 + 2] = rgb[2];
                frame.rgba8_pixels[p * 4 + 3] = alpha;
            }

            sprite.frames.push_back(std::move(frame));
        }
    }

    return sprite;
}

} // namespace quebratsk::parsers::goldsrc
