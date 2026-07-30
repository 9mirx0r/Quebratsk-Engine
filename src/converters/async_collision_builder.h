#pragma once

#include <vector>
#include <future>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace quebratsk::converters {

class AsyncCollisionBuilder {
public:
    /// Dispatches BVH/Concave polygon shape generation to a background thread
    static std::future<godot::Ref<godot::ConcavePolygonShape3D>> build_static_collision_async(
        const std::vector<godot::Vector3>& faces
    );
};

} // namespace quebratsk::converters
