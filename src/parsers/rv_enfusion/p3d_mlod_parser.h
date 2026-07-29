#pragma once

#include "../../core/ir/ir_mesh_data.h"
#include "../../core/ir/ir_skeleton_data.h"
#include "structs/p3d_structs.h"

#include <expected>
#include <span>
#include <string>

namespace quebratsk::parsers::rv_enfusion {

enum class P3DParseError {
    InvalidHeader,
    UnsupportedVersion,
    CorruptedData,
};

struct ParsedP3DModel {
    ir::IRMeshData mesh_data;
    ir::IRSkeletonData skeleton_data;
};

class P3DMLODParser {
public:
    /// Parse raw MLOD/ODOL P3D model binary data into Intermediate Representation
    [[nodiscard]] static std::expected<ParsedP3DModel, P3DParseError> parse(
        std::span<const std::byte> p3d_bytes
    );
};

} // namespace quebratsk::parsers::rv_enfusion
