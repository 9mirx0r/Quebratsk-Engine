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
    /// Parse StudioMDL v10 into IR mesh and skeleton.
    ///
    /// `texture_mdl_bytes` is the optional companion "<name>T.mdl". Many GoldSrc models
    /// — including much of the stock Counter-Strike 1.6 content — declare zero textures
    /// and keep them in that separate file; without it those models import with
    /// placeholder material names and no texture at all.
    [[nodiscard]] static std::expected<ParsedMDL10Model, MDL10ParseError> parse(
        std::span<const std::byte> mdl_bytes,
        std::span<const std::byte> texture_mdl_bytes = {}
    );
};

} // namespace quebratsk::parsers::goldsrc
