#pragma once

#include "../../core/ir/ir_mesh_data.h"
#include "../../core/ir/ir_skeleton_data.h"
#include "structs/mdl10_structs.h"

#include <expected>
#include <span>
#include <string>

namespace quebratsk::parsers::goldsrc {

enum class MDL10ParseError {
    InvalidHeader,
    VersionMismatch,
    CorruptedData,
};

struct ParsedMDL10Model {
    ir::IRMeshData mesh_data;
    ir::IRSkeletonData skeleton_data;
};

class MDL10Parser {
public:
    /// Parse raw StudioMDL v10 binary data into Intermediate Representation mesh & skeleton
    [[nodiscard]] static std::expected<ParsedMDL10Model, MDL10ParseError> parse(
        std::span<const std::byte> mdl_bytes
    );
};

} // namespace quebratsk::parsers::goldsrc
