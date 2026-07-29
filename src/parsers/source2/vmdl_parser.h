#pragma once

#include "../../core/ir/ir_mesh_data.h"
#include "../../core/ir/ir_skeleton_data.h"
#include "structs/vmdl_structs.h"

#include <expected>
#include <span>
#include <string>

namespace quebratsk::parsers::source2 {

enum class VMDLParseError {
    InvalidHeader,
    UnsupportedVersion,
    CorruptedData,
};

struct ParsedSource2Model {
    ir::IRMeshData mesh_data;
    ir::IRSkeletonData skeleton_data;
};

class VMDLParser {
public:
    /// Parse raw CS2 / Source 2 .vmdl_c compiled model binary data into IR structs
    [[nodiscard]] static std::expected<ParsedSource2Model, VMDLParseError> parse(
        std::span<const std::byte> vmdl_bytes
    );
};

} // namespace quebratsk::parsers::source2
