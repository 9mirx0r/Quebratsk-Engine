#include "collision_converter.h"
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include <cstring>

namespace quebratsk::converters {

using namespace godot;

Ref<ConvexPolygonShape3D> CollisionConverter::convert(const ir::IRCollisionData& ir_col) {
    Ref<ConvexPolygonShape3D> shape;
    shape.instantiate();

    // NOTE: all hulls are merged into a single convex shape. That is correct only
    // because VHACDDecomposer::generate_godot_shapes() calls this once per hull;
    // passing genuinely multi-hull data here silently produces one oversized shape.
    size_t total = 0;
    for (const auto& hull : ir_col.convex_hulls) {
        total += hull.vertices.size();
    }
    if (total == 0) return shape;

    // One allocation plus a direct write, instead of a CoW check per append().
    PackedVector3Array points;
    points.resize(static_cast<int64_t>(total));
    Vector3* w = points.ptrw();
    size_t offset = 0;
    for (const auto& hull : ir_col.convex_hulls) {
        if (hull.vertices.empty()) continue;
        std::memcpy(w + offset, hull.vertices.data(), hull.vertices.size() * sizeof(Vector3));
        offset += hull.vertices.size();
    }

    if (!points.is_empty()) {
        shape->set_points(points);
    }

    return shape;
}

} // namespace quebratsk::converters
