#include "mesh_converter.h"
#include "texture_loader.h"

#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cstring>

namespace quebratsk::converters {

using namespace godot;

Ref<ArrayMesh> MeshConverter::convert(const ir::IRMeshData& ir_mesh, TextureLoader* loader) {
    Ref<ArrayMesh> array_mesh;
    array_mesh.instantiate();

    for (const auto& surf : ir_mesh.surfaces) {
        if (surf.positions.empty() || surf.indices.empty()) {
            continue;
        }

        // Godot requires every per-vertex array in a surface to have the same element
        // count (ARRAY_TANGENT being 4 floats per vertex), and every index to be within
        // the vertex range. Feeding add_surface_from_arrays() anything else produces an
        // engine error at best and a corrupted vertex buffer in the driver at worst.
        const size_t vcount = surf.positions.size();
        if (!surf.normals.empty()  && surf.normals.size()  != vcount) continue;
        if (!surf.uv0.empty()      && surf.uv0.size()      != vcount) continue;
        if (!surf.uv1.empty()      && surf.uv1.size()      != vcount) continue;
        if (!surf.tangents.empty() && surf.tangents.size() != vcount * 4) continue;
        if (surf.indices.size() % 3 != 0) continue;

        const bool has_bad_index = std::any_of(
            surf.indices.begin(), surf.indices.end(),
            [vcount](uint32_t i) { return static_cast<size_t>(i) >= vcount; });
        if (has_bad_index) {
            UtilityFunctions::printerr("[MeshConverter] Index out of range in surface '",
                                       String(surf.material_name.c_str()), "', skipping.");
            continue;
        }

        Array surface_arrays;
        surface_arrays.resize(ArrayMesh::ARRAY_MAX);

        // 1. Vertices (ARRAY_VERTEX = 0) - Direct ptrw memcpy optimization
        PackedVector3Array vertices;
        vertices.resize(static_cast<int64_t>(surf.positions.size()));
        std::memcpy(vertices.ptrw(), surf.positions.data(), surf.positions.size() * sizeof(Vector3));
        surface_arrays[ArrayMesh::ARRAY_VERTEX] = vertices;

        // 2. Normals (ARRAY_NORMAL = 1)
        if (!surf.normals.empty()) {
            PackedVector3Array normals;
            normals.resize(static_cast<int64_t>(surf.normals.size()));
            std::memcpy(normals.ptrw(), surf.normals.data(), surf.normals.size() * sizeof(Vector3));
            surface_arrays[ArrayMesh::ARRAY_NORMAL] = normals;
        }

        // 3. Tangents (ARRAY_TANGENT = 2 - 4 floats per vertex)
        if (!surf.tangents.empty()) {
            PackedFloat32Array tangents;
            tangents.resize(static_cast<int64_t>(surf.tangents.size()));
            std::memcpy(tangents.ptrw(), surf.tangents.data(), surf.tangents.size() * sizeof(float));
            surface_arrays[ArrayMesh::ARRAY_TANGENT] = tangents;
        }

        // 4. Primary UVs (ARRAY_TEX_UV = 4)
        if (!surf.uv0.empty()) {
            PackedVector2Array uvs;
            uvs.resize(static_cast<int64_t>(surf.uv0.size()));
            std::memcpy(uvs.ptrw(), surf.uv0.data(), surf.uv0.size() * sizeof(Vector2));
            surface_arrays[ArrayMesh::ARRAY_TEX_UV] = uvs;
        }

        // 5. Lightmap UVs (ARRAY_TEX_UV2 = 5)
        if (!surf.uv1.empty()) {
            PackedVector2Array uv2s;
            uv2s.resize(static_cast<int64_t>(surf.uv1.size()));
            std::memcpy(uv2s.ptrw(), surf.uv1.data(), surf.uv1.size() * sizeof(Vector2));
            surface_arrays[ArrayMesh::ARRAY_TEX_UV2] = uv2s;
        }

        // 6. Indices (ARRAY_INDEX = 12)
        // Indices were validated above as < vcount, so every value fits in int32_t and
        // the uint32->int32 reinterpretation is exact. One memcpy beats a scalar loop.
        PackedInt32Array indices;
        indices.resize(static_cast<int64_t>(surf.indices.size()));
        std::memcpy(indices.ptrw(), surf.indices.data(), surf.indices.size() * sizeof(int32_t));
        surface_arrays[ArrayMesh::ARRAY_INDEX] = indices;

        array_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, surface_arrays);

        // Attach the surface's texture. IRSurface::material_name carries the legacy
        // texture reference (a WAD3 lump name for BSP, a VMT path for models); without
        // this step the UVs computed by the parsers had nothing to sample.
        if (loader && loader->is_valid() && !surf.material_name.empty()) {
            if (Ref<Texture2D> tex = loader->load(surf.material_name); tex.is_valid()) {
                Ref<StandardMaterial3D> mat;
                mat.instantiate();
                mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
                // Legacy content is authored for point sampling; bilinear filtering
                // blurs 64x64 GoldSrc textures into mush.
                mat->set_texture_filter(BaseMaterial3D::TEXTURE_FILTER_NEAREST_WITH_MIPMAPS);

                // Names beginning with '{' are GoldSrc's palette-keyed transparency.
                if (surf.material_name.front() == '{') {
                    mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
                }

                array_mesh->surface_set_material(array_mesh->get_surface_count() - 1, mat);
            }
        }
    }

    return array_mesh;
}

} // namespace quebratsk::converters
