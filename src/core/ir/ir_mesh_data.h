#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

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

    godot::Vector3 bbox_min;
    godot::Vector3 bbox_max;
};

} // namespace quebratsk::ir
