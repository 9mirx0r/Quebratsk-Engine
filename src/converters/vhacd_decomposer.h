#pragma once

#include "../core/ir/ir_mesh_data.h"
#include "../core/ir/ir_collision_data.h"

#include <godot_cpp/classes/convex_polygon_shape3d.hpp>
#include <godot_cpp/classes/ref.hpp>

#include <vector>

namespace quebratsk::converters {

struct VHACDParameters {
    uint32_t max_convex_hulls = 32;
    uint32_t resolution = 100000;
    float concavity = 0.001f;
    bool fill_mode_flood_fill = true;
};

class VHACDDecomposer {
public:
    /// Compute V-HACD 4.0 volumetric approximate convex decomposition of IRMeshData
    [[nodiscard]] static ir::IRCollisionData decompose(
        const ir::IRMeshData& ir_mesh,
        const VHACDParameters& params = {}
    );

    /// Convert decomposed IRCollisionData into array of Godot ConvexPolygonShape3D shapes
    [[nodiscard]] static std::vector<godot::Ref<godot::ConvexPolygonShape3D>> generate_godot_shapes(
        const ir::IRCollisionData& collision_data
    );
};

} // namespace quebratsk::converters
