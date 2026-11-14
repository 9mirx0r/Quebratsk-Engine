#pragma once

#include "../../core/ir/ir_mesh_data.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace quebratsk::parsers::source1 {

enum class VBSPParseError {
    InvalidHeader,
    VersionMismatch,
    TruncatedLump,
};

/// A level's baked light, laid out as one picture.
///
/// Source stores light as linear colour that can run well past white (a lamp's pool, the
/// sun on a wall), which eight bits a channel cannot hold. So it is kept as floats here and
/// becomes a half-float texture in Godot.
struct VBSPLightmap {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<float> rgb;   // width * height * 3, top-left origin, linear

    [[nodiscard]] bool is_valid() const {
        return width > 0 && height > 0 &&
               rgb.size() == static_cast<size_t>(width) * height * 3;
    }
};

/// The light a level stored for things that move through it (people, the weapon in hand,
/// props), which the light picture on the walls cannot light.
///
/// The level is cut into pieces of open space (leaves) by a tree of planes, and each leaf
/// keeps the light at a few spots inside it, as six colours: the light falling on a surface
/// that faces each way along the axes. Everything here is in Godot's axes and metres, and
/// the six colours are in Godot's order: +X, -X, +Y, -Y, +Z, -Z.
struct VBSPAmbient {
    struct Split {
        godot::Vector3 normal;
        float dist = 0.0f;
        int32_t front = 0;   // a child below zero is the leaf -(child + 1)
        int32_t back = 0;
    };
    struct Range {
        uint32_t first = 0;
        uint32_t count = 0;
    };

    int32_t root = 0;                     // the world's first split
    std::vector<Split> splits;
    std::vector<Range> leaves;            // which samples are each leaf's
    std::vector<godot::Vector3> spots;    // where each sample was taken
    std::vector<godot::Color> colours;    // six per sample, linear, can pass 1
    bool hdr = false;                     // the HDR copy was used

    [[nodiscard]] bool is_valid() const {
        return !splits.empty() && !leaves.empty() && colours.size() == spots.size() * 6;
    }

    /// The leaf `at` falls in, or -1.
    [[nodiscard]] int32_t leaf_at(const godot::Vector3& at) const;

    /// The six colours at `at`, blended from its leaf's samples by nearness. False when its
    /// leaf holds none (inside a wall, or outside the level).
    bool light_at(const godot::Vector3& at, godot::Color out[6]) const;
};

/// One of the props a level places by the thousand.
struct VBSPStaticProp {
    std::string model;            // "models/props/de_nuke/crate_small.mdl", lower case
    int32_t skin = 0;
    bool solid = false;
    godot::Transform3D transform; // where it stands, in Godot's axes and metres
};

/// One brush model: the world (element 0) or a brush entity's.
///
/// A Source brush entity's geometry is stored around the entity's origin, so `mesh` is in
/// the entity's own space and the entity's "origin" key says where it goes.
struct VBSPBrushModel {
    godot::Vector3 mins;
    godot::Vector3 maxs;
    ir::IRMeshData mesh;

    [[nodiscard]] godot::Vector3 centre() const { return (mins + maxs) * 0.5f; }
    [[nodiscard]] godot::Vector3 size() const { return maxs - mins; }
};

/// What the reader did with the level, for anyone checking it against the file.
struct VBSPStats {
    size_t faces_in_file = 0;
    size_t faces_drawn = 0;          // flat faces and displacements together
    size_t faces_not_drawn = 0;      // sky, triggers, tools, hints
    size_t displacements = 0;
    size_t displacements_bad = 0;    // not four corners, or their grid is missing
    size_t water_faces = 0;
    size_t textures = 0;
    size_t lit_faces = 0;
    size_t unlit_faces = 0;          // drawn without baked light
    size_t compressed_lumps = 0;     // LZMA lumps, which this reader does not open
    size_t light_samples = 0;        // spots where the level kept light for moving things
};

struct ParsedVBSPMap {
    int32_t version = 0;
    int32_t revision = 0;
    std::string entity_string;

    std::vector<VBSPBrushModel> brush_models;   // [0] is the world

    VBSPLightmap lightmap;
    bool hdr_light = false;   // the level's HDR light was used rather than its LDR copy

    /// Faces that show the sky. The sky itself is named by the world entity's "skyname".
    size_t sky_faces = 0;

    int32_t prop_lump_version = 0;
    std::vector<std::string> prop_models;
    std::vector<VBSPStaticProp> props;

    VBSPAmbient ambient;

    /// The zip packed into the level (lump 40): its own materials, models and cubemaps.
    std::span<const std::byte> pakfile;

    VBSPStats stats;
};

class VBSPParser {
public:
    [[nodiscard]] static bool is_vbsp(std::span<const std::byte> bytes);

    /// Read a Source level. `pakfile` in the result points into `bytes`.
    [[nodiscard]] static std::expected<ParsedVBSPMap, VBSPParseError> parse(
        std::span<const std::byte> bytes);

    /// How a level's pitch, yaw and roll (degrees, as in an "angles" key) turn something,
    /// in Godot's axes.
    [[nodiscard]] static godot::Basis angles_to_basis(float pitch, float yaw, float roll);
};

} // namespace quebratsk::parsers::source1
