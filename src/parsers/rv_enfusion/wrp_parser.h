#pragma once

#include "../../core/ir/ir_terrain_data.h"
#include "structs/wrp_structs.h"

#include <expected>
#include <span>
#include <string>

namespace quebratsk::parsers::rv_enfusion {

enum class WRPParseError {
    InvalidHeader,
    VersionMismatch,
    CorruptedData,
};

class WRPParser {
public:
    /// Parse raw Bohemia WRP terrain file binary data into IRTerrainData
    [[nodiscard]] static std::expected<ir::IRTerrainData, WRPParseError> parse(
        std::span<const std::byte> wrp_bytes
    );
};

} // namespace quebratsk::parsers::rv_enfusion
