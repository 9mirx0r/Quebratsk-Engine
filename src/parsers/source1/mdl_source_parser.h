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
    /// Parse Source 1 StudioMDL v44-49 binary data into Intermediate Representation.
    ///
    /// SCOPE: skeleton only. Unlike GoldSrc, a Source .mdl contains NO vertex data —
    /// positions, UVs and weights live in a companion .vvd, and the index/strip data
    /// lives in a .dx90.vtx. Producing mesh output therefore requires reading three
    /// files together, which this parser does not do. `mesh_data` is always empty.
    [[nodiscard]] static std::expected<ParsedSourceMDLModel, SourceMDLParseError> parse(
        std::span<const std::byte> mdl_bytes
    );
};

} // namespace quebratsk::parsers::source1
