#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ir_texture_data.h"

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

    godot::Vector3 bbox_min;
    godot::Vector3 bbox_max;
};

} // namespace quebratsk::ir
