#pragma once

#include "../../core/ir/ir_material_data.h"

#include <expected>
#include <span>
#include <string>

namespace quebratsk::parsers::source1 {

enum class VMTParseError {
    EmptyFile,
    InvalidSyntax,
};

class VMTParser {
public:
    /// Parse VMT KeyValues text into Intermediate Representation material parameters
    [[nodiscard]] static std::expected<ir::IRMaterialData, VMTParseError> parse(
        std::span<const std::byte> vmt_bytes
    );
};

} // namespace quebratsk::parsers::source1
