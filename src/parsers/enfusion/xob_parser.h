#pragma once

#include "../../core/ir/ir_mesh_data.h"
#include "../../core/ir/ir_skeleton_data.h"
#include "structs/xob_structs.h"

#include <expected>
#include <span>
#include <string>

namespace quebratsk::parsers::enfusion {

enum class XOBParseError {
    InvalidHeader,
    UnsupportedVersion,
    CorruptedData,
};

struct ParsedXOBModel {
    ir::IRMeshData mesh_data;
    ir::IRSkeletonData skeleton_data;
};

class XOBParser {
public:
    /// Parse raw Arma Reforger / Enfusion .xob compiled model binary data into IR structs
    [[nodiscard]] static std::expected<ParsedXOBModel, XOBParseError> parse(
        std::span<const std::byte> xob_bytes
    );
};

} // namespace quebratsk::parsers::enfusion
