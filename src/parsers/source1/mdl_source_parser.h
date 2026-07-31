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
#include <vector>

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

/// A .mdl together with the external animation-block file it defers its long sequences
/// to. `ani` may be empty, in which case blocked sequences are unreachable and skipped.
struct SourceAnimatedModel {
    std::span<const std::byte> mdl;
    std::span<const std::byte> ani;
};

/// The files a Source model is split across.
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
    std::span<const std::byte> ani;  // this model's own animation blocks, if any

    /// Models named by the .mdl's includemodel table, already fetched by the caller.
    /// GMod player models carry no animation of their own and borrow every sequence
    /// from a shared base model listed there, so without these they can only be shown
    /// in their bind pose. Each carries its own .ani: m_anm.mdl is 0.9 MB and defers
    /// 7.1 MB of stances to m_anm.ani.
    std::vector<SourceAnimatedModel> include_models;
};

/// How much of each animation sequence to recover.
///
/// A caller that only wants to *name* the sequences — filling a dropdown, say — still has
/// to know which of them decode, because a descriptor pointing at unreachable data is not
/// a pose the model can stand in. It does not need the resulting transforms. NamesOnly
/// applies the same acceptance rule and stops at the first bone that proves the sequence
/// is readable, instead of decoding every bone of all 359 of them.
enum class PoseDetail {
    Full,
    NamesOnly,
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
        const SourceModelBundle& bundle,
        PoseDetail poses = PoseDetail::Full
    );

    /// Paths this model borrows animation from, e.g. "models/player/cs_fix/anims.mdl".
    /// Cheap header-only read so a caller can fetch them before the real parse.
    [[nodiscard]] static std::vector<std::string> list_include_models(
        std::span<const std::byte> mdl_bytes
    );

    /// Path of the companion animation-block file this model defers long sequences to,
    /// e.g. "models/m_anm.ani". Empty when the model stores everything inline.
    [[nodiscard]] static std::string anim_block_name(std::span<const std::byte> mdl_bytes);
};

} // namespace quebratsk::parsers::source1
