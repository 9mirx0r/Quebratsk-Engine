#pragma once

#include "../../core/ir/ir_material_data.h"
#include "structs/wad3_structs.h"

#include <expected>
#include <span>
#include <string>
#include <vector>

namespace quebratsk::parsers::goldsrc {

enum class WAD3ParseError {
    InvalidHeader,
    LumpNotFound,
    InvalidMiptex,
    CorruptedData,
};

struct DecodedMiptex {
    std::string name;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba8_pixels; // RGBA8888 uncompressed pixel data
};

class WAD3Parser {
public:
    /// Parse a Miptex texture lump from WAD3 raw bytes into RGBA8 pixels
    [[nodiscard]] static std::expected<DecodedMiptex, WAD3ParseError> parse_miptex(
        std::span<const std::byte> miptex_bytes
    );
};

} // namespace quebratsk::parsers::goldsrc
