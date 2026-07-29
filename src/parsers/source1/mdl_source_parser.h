#pragma once

#include "../../core/ir/ir_mesh_data.h"
#include "../../core/ir/ir_skeleton_data.h"
#include "structs/studio_structs.h"

#include <expected>
#include <span>
#include <string>

namespace quebratsk::parsers::source1 {

enum class SourceMDLParseError {
    InvalidHeader,
    VersionMismatch,
    CorruptedData,
};

struct ParsedSourceMDLModel {
    ir::IRMeshData mesh_data;
    ir::IRSkeletonData skeleton_data;
};

class SourceMDLParser {
public:
    /// Parse Source 1 StudioMDL v44-49 binary data into Intermediate Representation
    [[nodiscard]] static std::expected<ParsedSourceMDLModel, SourceMDLParseError> parse(
        std::span<const std::byte> mdl_bytes
    );
};

} // namespace quebratsk::parsers::source1
