#pragma once

#include "../math/simd_math.h"
#include <godot_cpp/classes/occluder_instance3d.hpp>
#include <godot_cpp/classes/box_occluder3d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <future>
#include <vector>

namespace quebratsk::vfs {

class OcclusionGenerator {
public:
    /// Asynchronously calculates an OBB/AABB from vertex data and returns a Godot BoxOccluder3D
    static std::future<godot::Ref<godot::BoxOccluder3D>> generate_async(const std::vector<godot::Vector3>& vertices);
    
    /// Synchronously creates the OccluderInstance3D node with the generated shape
    static godot::OccluderInstance3D* create_instance(const godot::Ref<godot::BoxOccluder3D>& occluder);
};

} // namespace quebratsk::vfs
