#include "collision_converter.h"
#include <godot_cpp/variant/packed_vector3_array.hpp>

namespace quebratsk::converters {

using namespace godot;

Ref<ConvexPolygonShape3D> CollisionConverter::convert(const ir::IRCollisionData& ir_col) {
    Ref<ConvexPolygonShape3D> shape;
    shape.instantiate();

    PackedVector3Array points;
    for (const auto& hull : ir_col.convex_hulls) {
        for (const auto& v : hull.vertices) {
            points.append(v);
        }
    }

    if (!points.is_empty()) {
        shape->set_points(points);
    }

    return shape;
}

} // namespace quebratsk::converters
