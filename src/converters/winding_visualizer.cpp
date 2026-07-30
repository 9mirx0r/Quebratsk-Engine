#include "winding_visualizer.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

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

    // This used to only clear the material override — it never flipped anything,
    // despite the name and despite being exposed to GDScript.
    Ref<Mesh> source = mesh_instance->get_mesh();
    if (source.is_null()) {
        ERR_PRINT("[WindingVisualizer] MeshInstance3D has no mesh to flip.");
        return;
    }

    Ref<ArrayMesh> flipped;
    flipped.instantiate();

    const int surface_count = source->get_surface_count();
    for (int s = 0; s < surface_count; ++s) {
        Array arrays = source->surface_get_arrays(s);
        if (arrays.size() != Mesh::ARRAY_MAX) continue;

        // Reverse each triangle's winding: (a, b, c) -> (a, c, b).
        if (arrays[Mesh::ARRAY_INDEX].get_type() == Variant::PACKED_INT32_ARRAY) {
            PackedInt32Array indices = arrays[Mesh::ARRAY_INDEX];
            for (int64_t i = 0; i + 2 < indices.size(); i += 3) {
                const int32_t tmp = indices[i + 1];
                indices.set(i + 1, indices[i + 2]);
                indices.set(i + 2, tmp);
            }
            arrays[Mesh::ARRAY_INDEX] = indices;
        }

        // Negate normals so lighting matches the new facing.
        if (arrays[Mesh::ARRAY_NORMAL].get_type() == Variant::PACKED_VECTOR3_ARRAY) {
            PackedVector3Array normals = arrays[Mesh::ARRAY_NORMAL];
            Vector3* w = normals.ptrw();
            for (int64_t i = 0; i < normals.size(); ++i) {
                w[i] = -w[i];
            }
            arrays[Mesh::ARRAY_NORMAL] = normals;
        }

        // Tangent handedness (every 4th float) follows the normal flip.
        if (arrays[Mesh::ARRAY_TANGENT].get_type() == Variant::PACKED_FLOAT32_ARRAY) {
            PackedFloat32Array tangents = arrays[Mesh::ARRAY_TANGENT];
            float* w = tangents.ptrw();
            for (int64_t i = 3; i < tangents.size(); i += 4) {
                w[i] = -w[i];
            }
            arrays[Mesh::ARRAY_TANGENT] = tangents;
        }

        flipped->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);

        if (Ref<Material> mat = source->surface_get_material(s); mat.is_valid()) {
            flipped->surface_set_material(flipped->get_surface_count() - 1, mat);
        }
    }

    if (flipped->get_surface_count() == 0) {
        ERR_PRINT("[WindingVisualizer] No triangle surfaces were flipped.");
        return;
    }

    mesh_instance->set_mesh(flipped);
    mesh_instance->set_material_override(Ref<Material>()); // clear the debug shader
}

} // namespace quebratsk::converters
