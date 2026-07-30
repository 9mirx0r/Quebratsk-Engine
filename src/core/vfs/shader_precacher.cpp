#include "shader_precacher.h"
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/core/error_macros.hpp>

namespace quebratsk::vfs {

using namespace godot;

void ShaderPrecacher::precache_all_materials(const std::vector<std::string>& vfs_material_paths) {
    // NOT IMPLEMENTED. The previous body built an identical trivial dummy shader for
    // every path and immediately dropped the Ref, so the RID was freed before the
    // driver ever compiled anything: it precached nothing while paying the cost of
    // creating and destroying one material per entry.
    //
    // A real implementation must build the actual material for each path (via
    // VMTParser + MaterialConverter) and keep it alive long enough for the pipeline
    // state object to be created.
    if (!vfs_material_paths.empty()) {
        ERR_PRINT_ONCE("[ShaderPrecacher] precache_all_materials() is not implemented; "
                       "no shaders are precompiled.");
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
