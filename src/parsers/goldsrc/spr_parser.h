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

struct DecodedSpriteFrame {
    int32_t width = 0;
    int32_t height = 0;
    int32_t origin_x = 0;
    int32_t origin_y = 0;
    std::vector<uint8_t> rgba8_pixels;
};

struct ParsedSprite {
    int32_t type = 0;
    int32_t max_width = 0;
    int32_t max_height = 0;
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
