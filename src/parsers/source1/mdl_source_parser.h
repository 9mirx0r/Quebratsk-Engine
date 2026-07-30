#pragma once

#include "../../core/ir/ir_mesh_data.h"
#include "../../core/ir/ir_skeleton_data.h"
#include "structs/anim_structs.h"
#include "structs/studio_structs.h"
#include "structs/vtx_structs.h"
#include "structs/vvd_structs.h"

#include <expected>
#include <span>
#include <string>

namespace quebratsk::parsers::source1 {

enum class SourceMDLParseError {
    InvalidHeader,
    VersionMismatch,
    CorruptedData,
    MissingCompanionFile,
    ChecksumMismatch,
};

struct ParsedSourceMDLModel {
    ir::IRMeshData mesh_data;
    ir::IRSkeletonData skeleton_data;
};

/// The three files a Source model is split across.
///
/// A .mdl on its own carries bones, body-part structure and material names but NO
/// vertex data whatsoever. Positions, normals, UVs and weights live in the .vvd, and
/// the index/strip data in the .dx90.vtx. All three must be read together.
///
/// `vvd` and `vtx` may be empty spans, in which case only the skeleton is produced.
struct SourceModelBundle {
    std::span<const std::byte> mdl;
    std::span<const std::byte> vvd;
    std::span<const std::byte> vtx;
};

class SourceMDLParser {
public:
    /// Skeleton-only parse from a lone .mdl. Kept for callers that have no companions;
    /// `mesh_data` comes back empty because the format simply does not contain it.
    [[nodiscard]] static std::expected<ParsedSourceMDLModel, SourceMDLParseError> parse(
        std::span<const std::byte> mdl_bytes
    );

    /// Full parse: skeleton from the .mdl, geometry assembled from .vvd + .vtx.
    ///
    /// Pure data in, pure data out — no Godot Object or Resource is created, so this is
    /// safe to run off the main thread.
    [[nodiscard]] static std::expected<ParsedSourceMDLModel, SourceMDLParseError> parse_bundle(
        const SourceModelBundle& bundle
    );
};

} // namespace quebratsk::parsers::source1
