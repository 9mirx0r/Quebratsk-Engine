#include "shader_precacher.h"
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/shader.hpp>

namespace quebratsk::vfs {

using namespace godot;

void ShaderPrecacher::precache_all_materials(const std::vector<std::string>& vfs_material_paths) {
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!rs) return;

    for (const auto& path : vfs_material_paths) {
        Ref<ShaderMaterial> mat = _create_dummy_material(path);
        if (mat.is_valid() && mat->get_shader().is_valid()) {
            // Force compilation by asking the RenderingServer to instance the material
            // Alternatively, setting it to a dummy mesh or using material_get_shader
            RID mat_rid = mat->get_rid();
            
            // This is a Godot 4 specific trick to force compilation
            // Usually, requesting the shader's valid state or attaching it triggers it.
            rs->material_get_shader(mat_rid);
        }
    }
}

Ref<ShaderMaterial> ShaderPrecacher::_create_dummy_material(const std::string& path) {
    // In a real implementation, this would parse the .vmt or .rvmat and build the material.
    // For now, we return an empty ShaderMaterial stub.
    Ref<ShaderMaterial> mat;
    mat.instantiate();
    
    Ref<Shader> shader;
    shader.instantiate();
    shader->set_code("shader_type spatial; void fragment() { ALBEDO = vec3(1.0); }");
    mat->set_shader(shader);
    
    return mat;
}

} // namespace quebratsk::vfs
