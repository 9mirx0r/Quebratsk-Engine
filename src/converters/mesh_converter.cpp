#include "mesh_converter.h"

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

namespace quebratsk::converters {

using namespace godot;

Ref<ArrayMesh> MeshConverter::convert(const ir::IRMeshData& ir_mesh) {
    Ref<ArrayMesh> array_mesh;
    array_mesh.instantiate();

    for (const auto& surf : ir_mesh.surfaces) {
        if (surf.positions.empty() || surf.indices.empty()) {
            continue;
        }

        Array surface_arrays;
        surface_arrays.resize(ArrayMesh::ARRAY_MAX);

        // 1. Vertices (ARRAY_VERTEX = 0)
        PackedVector3Array vertices;
        vertices.resize(static_cast<int64_t>(surf.positions.size()));
        for (size_t i = 0; i < surf.positions.size(); ++i) {
            vertices.set(static_cast<int64_t>(i), surf.positions[i]);
        }
        surface_arrays[ArrayMesh::ARRAY_VERTEX] = vertices;

        // 2. Normals (ARRAY_NORMAL = 1)
        if (!surf.normals.empty()) {
            PackedVector3Array normals;
            normals.resize(static_cast<int64_t>(surf.normals.size()));
            for (size_t i = 0; i < surf.normals.size(); ++i) {
                normals.set(static_cast<int64_t>(i), surf.normals[i]);
            }
            surface_arrays[ArrayMesh::ARRAY_NORMAL] = normals;
        }

        // 3. Tangents (ARRAY_TANGENT = 2 - 4 floats per vertex)
        if (!surf.tangents.empty()) {
            PackedFloat32Array tangents;
            tangents.resize(static_cast<int64_t>(surf.tangents.size()));
            for (size_t i = 0; i < surf.tangents.size(); ++i) {
                tangents.set(static_cast<int64_t>(i), surf.tangents[i]);
            }
            surface_arrays[ArrayMesh::ARRAY_TANGENT] = tangents;
        }

        // 4. Primary UVs (ARRAY_TEX_UV = 4)
        if (!surf.uv0.empty()) {
            PackedVector2Array uvs;
            uvs.resize(static_cast<int64_t>(surf.uv0.size()));
            for (size_t i = 0; i < surf.uv0.size(); ++i) {
                uvs.set(static_cast<int64_t>(i), surf.uv0[i]);
            }
            surface_arrays[ArrayMesh::ARRAY_TEX_UV] = uvs;
        }

        // 5. Lightmap UVs (ARRAY_TEX_UV2 = 5)
        if (!surf.uv1.empty()) {
            PackedVector2Array uv2s;
            uv2s.resize(static_cast<int64_t>(surf.uv1.size()));
            for (size_t i = 0; i < surf.uv1.size(); ++i) {
                uv2s.set(static_cast<int64_t>(i), surf.uv1[i]);
            }
            surface_arrays[ArrayMesh::ARRAY_TEX_UV2] = uv2s;
        }

        // 6. Indices (ARRAY_INDEX = 12)
        PackedInt32Array indices;
        indices.resize(static_cast<int64_t>(surf.indices.size()));
        for (size_t i = 0; i < surf.indices.size(); ++i) {
            indices.set(static_cast<int64_t>(i), static_cast<int32_t>(surf.indices[i]));
        }
        surface_arrays[ArrayMesh::ARRAY_INDEX] = indices;

        array_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, surface_arrays);
    }

    return array_mesh;
}

} // namespace quebratsk::converters
