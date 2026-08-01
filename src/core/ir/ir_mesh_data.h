#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ir_texture_data.h"

#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace quebratsk::ir {

enum class SourceEngine {
    GoldSrc,
    Source1,
    Source2,
    RealVirtuality,
    Enfusion,
};

/// Surface representation (single material binding)
struct IRSurface {
    std::string material_name;

    /// Index into IRMeshData::embedded_textures, or -1 when the texture must be
    /// resolved through the VFS by `material_name` instead. GoldSrc .mdl files carry
    /// their textures inside the model file, so there is nothing to look up.
    int32_t embedded_texture_index = -1;

    std::vector<godot::Vector3> positions;
    std::vector<godot::Vector3> normals;
    std::vector<godot::Vector2> uv0;   // Primary UV map
    std::vector<godot::Vector2> uv1;   // Lightmap UV map (BSP)
    std::vector<float> tangents;        // 4 floats per vertex (x,y,z,w)
    std::vector<godot::Color> colors;   // Vertex colors
    std::vector<uint32_t> indices;      // Triangle indices

    // Bone skinning (up to 4 influences per vertex)
    std::vector<std::array<int32_t, 4>> bone_indices;
    std::vector<std::array<float, 4>> bone_weights;
};

/// Complete mesh data in Intermediate Representation
/// A part of a model that comes in several versions, only one of which is worn at a time.
///
/// A Half-Life scientist has four heads, a weapon has a magazine that can be present or
/// absent, a player model has a helmet. The game picks one per part at runtime through a
/// single packed integer, and an importer that quietly takes the first is not wrong so much
/// as silent: somebody looking for the model with the glasses has no way to know it is in
/// there.
struct IRBodyGroup {
    std::string name;                    // the part, e.g. "head" or "clip"
    std::vector<std::string> options;    // what it can be, in the order the model lists them
    int32_t chosen = 0;                  // which one this import actually built
};

struct IRMeshData {
    SourceEngine source_engine = SourceEngine::GoldSrc;
    std::string name;
    std::vector<IRSurface> surfaces;

    /// Textures shipped inside the asset itself (GoldSrc .mdl). Referenced by
    /// IRSurface::embedded_texture_index.
    std::vector<IRTextureData> embedded_textures;

    /// Where this asset says its materials live, e.g. "models/player/police/".
    ///
    /// A Source model names its materials ("police") and, separately, the directories to
    /// look for them in. Without the second half a loader can only search the whole index
    /// for something ending in "/police.vmt", which finds the wrong file as easily as the
    /// right one and, often enough, nothing: that is why models arrived untextured.
    std::vector<std::string> material_search_paths;

    /// Where the model itself says its body reaches to, in Godot's axes and metres.
    ///
    /// This is not the same thing as the mesh's bounding box and the difference is the whole
    /// reason it is here. A GoldSrc entity's origin is the centre of its hull rather than the
    /// soles of its feet — a player is 72 units tall and stands with its origin 36 units up —
    /// and the animations are authored around that. The bind pose is authored standing on the
    /// origin instead, so a collider measured from the mesh sits most of a metre away from the
    /// body that is actually drawn, and the character appears to float.
    ///
    /// Taken from the sequences, each of which declares its own bbmin and bbmax. Empty when
    /// nothing declared any, which is a model whose collider has to be guessed at.
    bool has_declared_bounds = false;
    godot::AABB declared_bounds;

    /// The parts of this model that have alternatives, and which one was built. Empty for a
    /// model with nothing to choose, which is most props.
    std::vector<IRBodyGroup> body_groups;

    /// Named points on the skeleton the game hangs things from: a muzzle flash, a shell
    /// ejection, the tip of a barrel. Each is a bone and an offset in that bone's space, and
    /// they are the only record of where a weapon's effects belong.
    struct Attachment {
        std::string name;
        int32_t bone_index = -1;
        godot::Vector3 position;         // in the bone's own space, already in Godot's axes
    };
    std::vector<Attachment> attachments;

    godot::Vector3 bbox_min;
    godot::Vector3 bbox_max;
};

} // namespace quebratsk::ir
