#include "async_collision_builder.h"

namespace quebratsk::converters {

using namespace godot;

std::future<CollisionResult> AsyncCollisionBuilder::prepare_faces_async(
    const std::vector<Vector3>& raw_faces
) {
    return std::async(std::launch::async, [raw_faces]() -> CollisionResult {
        if (raw_faces.empty()) return {};

        PackedVector3Array godot_faces;
        godot_faces.resize(raw_faces.size());

        for (size_t i = 0; i < raw_faces.size(); ++i) {
            godot_faces.set(i, raw_faces[i]);
        }

        // Pure data — no Godot physics objects created here
        return { std::move(godot_faces) };
    });
}

Ref<ConcavePolygonShape3D> AsyncCollisionBuilder::create_shape(const CollisionResult& result) {
    // MUST be called from main thread only — set_faces() talks to PhysicsServer3D
    if (result.faces.is_empty()) return {};

    Ref<ConcavePolygonShape3D> shape;
    shape.instantiate();
    shape->set_faces(result.faces);
    return shape;
}

} // namespace quebratsk::converters
