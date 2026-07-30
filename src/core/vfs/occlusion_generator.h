#pragma once

#include "../math/simd_math.h"
#include <godot_cpp/classes/occluder_instance3d.hpp>
#include <godot_cpp/classes/box_occluder3d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <future>
#include <vector>

namespace quebratsk::vfs {

/// Pure data result from background AABB computation (no Godot objects)
struct OcclusionResult {
    godot::Vector3 size;
    godot::Vector3 center;
};

class OcclusionGenerator {
public:
    /// Asynchronously computes AABB bounds from vertex data (pure math, thread-safe)
    static std::future<OcclusionResult> compute_bounds_async(const std::vector<godot::Vector3>& vertices);

    /// Creates Godot OccluderInstance3D from precomputed result — MUST be called from main thread
    static godot::OccluderInstance3D* create_from_result(const OcclusionResult& result);
};

} // namespace quebratsk::vfs
