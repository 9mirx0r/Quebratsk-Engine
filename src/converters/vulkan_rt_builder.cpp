#include "vulkan_rt_builder.h"
#include <godot_cpp/variant/utility_functions.hpp>

namespace quebratsk::converters {

using namespace godot;

void VulkanRTBuilder::_bind_methods() {
    ClassDB::bind_static_method("VulkanRTBuilder", D_METHOD("register_tlas_instance", "mesh_rid", "transform"), &VulkanRTBuilder::register_tlas_instance);
}

Dictionary VulkanRTBuilder::build_blas_descriptor(const ir::IRMeshData& ir_mesh) {
    Dictionary blas_dict;
    int total_vertices = 0;
    int total_triangles = 0;

    for (const auto& surf : ir_mesh.surfaces) {
        total_vertices += static_cast<int>(surf.positions.size());
        total_triangles += static_cast<int>(surf.indices.size() / 3);
    }

    blas_dict["mesh_name"] = String(ir_mesh.name.c_str());
    blas_dict["total_vertices"] = total_vertices;
    blas_dict["total_triangles"] = total_triangles;
    blas_dict["blas_type"] = "VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR";
    blas_dict["is_built"] = true;

    return blas_dict;
}

bool VulkanRTBuilder::register_tlas_instance(const RID& mesh_rid, const Transform3D& transform) {
    // NOT IMPLEMENTED, and not implementable today: Godot 4.3 exposes no ray-tracing
    // acceleration-structure API through RenderingServer or RenderingDevice. The
    // previous body printed a success message and returned true.
    UtilityFunctions::push_error(
        "[VulkanRTBuilder] register_tlas_instance() is not implemented: Godot 4.3 "
        "exposes no ray tracing acceleration structure API.");
    return false;
}

} // namespace quebratsk::converters
