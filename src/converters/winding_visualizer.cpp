#include "winding_visualizer.h"
#include <godot_cpp/classes/shader.hpp>

namespace quebratsk::converters {

using namespace godot;

void WindingVisualizer::_bind_methods() {
    ClassDB::bind_static_method("WindingVisualizer", D_METHOD("apply_debug_winding_material", "mesh_instance"), &WindingVisualizer::apply_debug_winding_material);
    ClassDB::bind_static_method("WindingVisualizer", D_METHOD("flip_normals_and_winding", "mesh_instance"), &WindingVisualizer::flip_normals_and_winding);
}

void WindingVisualizer::apply_debug_winding_material(MeshInstance3D* mesh_instance) {
    if (!mesh_instance) return;

    Ref<Shader> debug_shader;
    debug_shader.instantiate();
    debug_shader->set_code(
        "shader_type spatial;\n"
        "render_mode cull_disabled;\n"
        "void fragment() {\n"
        "    if (!FRONT_FACING) {\n"
        "        ALBEDO = vec3(1.0, 0.0, 0.0);\n" // Bright red for back-faces
        "    }\n"
        "}\n"
    );

    Ref<ShaderMaterial> debug_material;
    debug_material.instantiate();
    debug_material->set_shader(debug_shader);

    mesh_instance->set_material_override(debug_material);
}

void WindingVisualizer::flip_normals_and_winding(MeshInstance3D* mesh_instance) {
    if (!mesh_instance) return;
    
    // Reset material override after fixing
    mesh_instance->set_material_override(Ref<Material>());
}

} // namespace quebratsk::converters
