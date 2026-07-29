#pragma once

#include "../core/ir/ir_collision_data.h"

#include <godot_cpp/classes/convex_polygon_shape3d.hpp>
#include <godot_cpp/classes/ref.hpp>

namespace quebratsk::converters {

class CollisionConverter {
public:
    /// Convert Intermediate Representation IRCollisionData into native Godot 4 ConvexPolygonShape3D
    [[nodiscard]] static godot::Ref<godot::ConvexPolygonShape3D> convert(const ir::IRCollisionData& ir_col);
};

} // namespace quebratsk::converters
