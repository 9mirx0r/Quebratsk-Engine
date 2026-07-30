#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>
#include "../core/ir/ir_mesh_data.h"

namespace quebratsk::converters {

/// Vulkan Ray Tracing Acceleration Structure Builder (BLAS & TLAS)
class VulkanRTBuilder : public godot::Object {
    GDCLASS(VulkanRTBuilder, godot::Object)

protected:
    static void _bind_methods();

public:
    VulkanRTBuilder() = default;
    ~VulkanRTBuilder() override = default;

    /// Generates Bottom-Level Acceleration Structure (BLAS) geometry buffer descriptor for Vulkan Ray Tracing
    static godot::Dictionary build_blas_descriptor(const ir::IRMeshData& ir_mesh);

    /// Registers Top-Level Acceleration Structure (TLAS) instance into RenderingServer
    static bool register_tlas_instance(const godot::RID& mesh_rid, const godot::Transform3D& transform);
};

} // namespace quebratsk::converters
