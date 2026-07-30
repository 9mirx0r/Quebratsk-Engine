#pragma once

#include <vector>
#include <future>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

namespace quebratsk::converters {

/// Pure data result from background face preparation (no Godot physics objects)
struct CollisionResult {
    godot::PackedVector3Array faces;
};

class AsyncCollisionBuilder {
public:
    /// Prepares face data in background thread (pure data copy, thread-safe)
    static std::future<CollisionResult> prepare_faces_async(
        const std::vector<godot::Vector3>& raw_faces
    );

    /// Creates ConcavePolygonShape3D from precomputed result — MUST be called from main thread
    static godot::Ref<godot::ConcavePolygonShape3D> create_shape(const CollisionResult& result);
};

} // namespace quebratsk::converters
