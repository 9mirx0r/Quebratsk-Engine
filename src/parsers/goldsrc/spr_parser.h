#pragma once

#include "structs/spr_structs.h"

#include <expected>
#include <span>
#include <vector>

namespace quebratsk::parsers::goldsrc {

enum class SPRParseError {
    InvalidHeader,
    VersionMismatch,
    CorruptedData,
};

/// How the engine blends a sprite (header field `tex_format`). Names and values come
/// from the public Half-Life SDK (utils/sprgen/spritegn.h).
enum class SpriteTexFormat : int32_t {
    Normal = 0,     ///< Opaque.
    Additive = 1,   ///< Opaque pixels, drawn additively: black adds nothing.
    IndexAlpha = 2, ///< The palette index is the alpha; every pixel takes palette[255] as colour.
    AlphaTest = 3,  ///< Palette index 255 is fully transparent, everything else opaque.
};

struct DecodedSpriteFrame {
    int32_t width = 0;
    int32_t height = 0;
    int32_t origin_x = 0;
    int32_t origin_y = 0;
    /// Seconds from the start of the group to the end of this frame. Zero outside groups.
    float group_interval = 0.0f;
    std::vector<uint8_t> rgba8_pixels;
};

struct ParsedSprite {
    int32_t type = 0;
    /// Raw header value. Values outside SpriteTexFormat are decoded as Normal.
    int32_t tex_format = 0;
    int32_t max_width = 0;
    int32_t max_height = 0;
    /// False when the file ended, or stopped making sense, before every declared frame
    /// was read. `frames` then holds only the frames that decoded cleanly.
    bool complete = true;
    std::vector<DecodedSpriteFrame> frames;
};

class SPRParser {
public:
    /// Parse raw GoldSrc .spr binary data into decoded RGBA8 frames
    [[nodiscard]] static std::expected<ParsedSprite, SPRParseError> parse(
        std::span<const std::byte> spr_bytes
    );
};

} // namespace quebratsk::parsers::goldsrc
