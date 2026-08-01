#pragma once

#include "../../core/ir/ir_collision_data.h"
#include "../../core/ir/ir_mesh_data.h"
#include "structs/bsp30_structs.h"

#include <expected>
#include <span>
#include <string>

#include <godot_cpp/variant/vector3.hpp>

namespace quebratsk::parsers::goldsrc {

enum class BSP30ParseError {
    InvalidHeader,
    VersionMismatch,
    TruncatedLump,
    CorruptedData,
};

/// One brush model's extent, in Godot's axes and metres.
///
/// Half a map's entities are brushes rather than points: a door, a lift, a trigger volume, a
/// pool of water. Those carry no "origin" at all — they carry `"model" "*71"`, and where they
/// are IS that model. An importer that reads origins and nothing else sees a level with holes
/// in it: on de_aztec that is one rain volume, five bodies of water and both of the triggers
/// that set off the thunder, none of which have a position to read.
struct BrushModelBounds {
    godot::Vector3 mins;
    godot::Vector3 maxs;

    [[nodiscard]] godot::Vector3 centre() const { return (mins + maxs) * 0.5f; }
    [[nodiscard]] godot::Vector3 size() const { return maxs - mins; }
};

struct ParsedBSP30Map {
    ir::IRMeshData mesh_data;
    ir::IRCollisionData collision_data;
    std::string entity_string; // Raw ASCII entities lump

    /// Indexed the way an entity names them: element N is what "*N" refers to. Element 0 is
    /// the world itself, which no entity points at but which is what the map's own size is.
    std::vector<BrushModelBounds> brush_models;
};

class BSP30Parser {
public:
    /// Parse raw BSP v30 file data into Intermediate Representation mesh & collision
    [[nodiscard]] static std::expected<ParsedBSP30Map, BSP30ParseError> parse(
        std::span<const std::byte> bsp_bytes
    );
};

} // namespace quebratsk::parsers::goldsrc
