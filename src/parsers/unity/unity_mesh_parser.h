#pragma once

#include "../../core/ir/ir_mesh_data.h"
#include "../../core/ir/ir_skeleton_data.h"

#include <expected>
#include <span>
#include <string>

namespace quebratsk::parsers::unity {

enum class UnityMeshParseError {
    InvalidHeader,
    CorruptedData,
};

struct ParsedUnityModel {
    ir::IRMeshData mesh_data;
    ir::IRSkeletonData skeleton_data;
};

class UnityMeshParser {
public:
    /// Parse raw serialized Unity 3D Mesh object into Intermediate Representation
    [[nodiscard]] static std::expected<ParsedUnityModel, UnityMeshParseError> parse(
        std::span<const std::byte> mesh_bytes
    );
};

} // namespace quebratsk::parsers::unity
