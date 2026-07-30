#pragma once

#include "../../core/ir/ir_material_data.h"
#include "../../core/ir/ir_texture_data.h"
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

class WAD3Parser {
public:
    /// Parse a Miptex texture lump from WAD3 raw bytes into RGBA8 pixels.
    /// Returns the shared IRTextureData so TextureConverter can consume it directly;
    /// the former DecodedMiptex struct was identical but incompatible by type.
    [[nodiscard]] static std::expected<ir::IRTextureData, WAD3ParseError> parse_miptex(
        std::span<const std::byte> miptex_bytes
    );
};

} // namespace quebratsk::parsers::goldsrc
