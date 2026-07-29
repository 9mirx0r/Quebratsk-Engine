#pragma once

#include "../../core/ir/ir_mesh_data.h"
#include "../../core/ir/ir_skeleton_data.h"
#include "structs/uasset_structs.h"

#include <expected>
#include <span>
#include <string>

namespace quebratsk::parsers::unreal {

enum class UAssetParseError {
    InvalidHeader,
    CorruptedData,
};

struct ParsedUnrealAsset {
    ir::IRMeshData mesh_data;
    ir::IRSkeletonData skeleton_data;
};

class UAssetParser {
public:
    /// Parse raw Unreal Engine .uasset / .uexp package binary data into IR structs
    [[nodiscard]] static std::expected<ParsedUnrealAsset, UAssetParseError> parse(
        std::span<const std::byte> uasset_bytes
    );
};

} // namespace quebratsk::parsers::unreal
