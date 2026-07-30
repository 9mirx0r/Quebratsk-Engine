#include "async_collision_builder.h"
#include <godot_cpp/variant/packed_vector3_array.hpp>

namespace quebratsk::converters {

using namespace godot;

std::future<Ref<ConcavePolygonShape3D>> AsyncCollisionBuilder::build_static_collision_async(
    const std::vector<Vector3>& faces
) {
    return std::async(std::launch::async, [faces]() -> Ref<ConcavePolygonShape3D> {
        if (faces.empty()) return Ref<ConcavePolygonShape3D>();

        PackedVector3Array godot_faces;
        godot_faces.resize(faces.size());
        
        for (size_t i = 0; i < faces.size(); ++i) {
            godot_faces.set(i, faces[i]);
        }

        Ref<ConcavePolygonShape3D> shape;
        shape.instantiate();
        
        // This is a computationally heavy operation building the BVH
        shape->set_faces(godot_faces);
        
        return shape;
    });
}

} // namespace quebratsk::converters
