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

    /// This model's own geometry, separate from the world's.
    ///
    /// Faces used to be grouped by texture across the whole map into one mesh, which meant a
    /// door's faces were merged with every wall sharing its texture and there was nothing
    /// left to move. A door cannot open, a lift cannot rise and a decoration cannot stop
    /// being solid while they are all the same object.
    ///
    /// Empty for a model with nothing visible, which some trigger volumes are.
    ir::IRMeshData mesh;

    /// What is solid in this model, as convex blocks in Godot's axes and metres. See
    /// build_model_solids() for where they come from and what they leave out. Empty for a
    /// model with nothing solid in it, such as a body of water.
    ///
    /// For the world (element 0) this is the same list as ParsedBSP30Map::collision_data.
    std::vector<ir::IRConvexHull> solids;

    /// The same model as the game's standing player meets it: every solid fattened by half
    /// a player, clip brushes included. Only for testing the point at a player's middle;
    /// see build_clip_solids(). Empty when the map has no such tree for this model.
    std::vector<ir::IRConvexHull> player_solids;
};

struct ParsedBSP30Map {
    ir::IRMeshData mesh_data;

    /// The world's solid parts as convex blocks (`convex_hulls`), read from the map's own
    /// tree rather than guessed from what is drawn. Brush entities keep theirs in
    /// `brush_models[n].solids`. Empty only for a file with no tree or no model lump.
    ir::IRCollisionData collision_data;
    std::string entity_string; // Raw ASCII entities lump

    /// How many faces were left out because the sky is seen through them. Zero for a level
    /// with no outdoors, which then needs no sky drawn behind it.
    int sky_faces = 0;

    /// Every face's baked light, packed into one picture. Empty when the map ships no
    /// lighting, in which case no surface carries `uv1` either.
    ///
    /// GoldSrc stores light as a small RGB grid per face, one sample every 16 units, all laid
    /// end to end in lump 8. Here each grid sits in its own rectangle of this atlas and the
    /// face's `uv1` points into it. A face with no light of its own (water, sky, a bad
    /// offset) points at a white patch in the corner, which leaves its texture as it is.
    ///
    /// A face can carry up to four grids, one per light style: the steady light plus lights
    /// that flicker or can be switched. They are summed here, which is the level with every
    /// light on and steady. Telling them apart again is work for whoever animates them.
    ir::IRTextureData lightmap;

    /// True when the map stores planes in lump 0 and entities in lump 1 (Blue Shift).
    bool blue_shift_layout = false;

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
