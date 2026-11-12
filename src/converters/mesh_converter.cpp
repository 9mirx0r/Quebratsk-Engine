#include "mesh_converter.h"
#include "texture_converter.h"
#include "texture_loader.h"

#include <godot_cpp/classes/shader_material.hpp>
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

namespace {

/// Texture times light, the way the game did it.
///
/// Both pictures are read as the plain numbers stored in them and multiplied as such,
/// because that is what a 1998 renderer did; only the product is converted for Godot's
/// linear pipeline. Multiplying after conversion instead darkens every mid-tone.
///
/// The stored numbers are not what reached the screen, though. The engine ran both through
/// tables built from the player's video options, and a level drawn without them is far too
/// dark (a night map comes out almost black). With the options at their defaults (gamma 2.5,
/// texgamma 2.0, lightgamma 2.5) the tables amount to this, as documented by the open
/// Xash3D engine's gamma and lightmap code:
///
///   texture  = stored ^ (texgamma / gamma) = stored ^ 0.8
///   light    = min(stored * 2^(1 / lightgamma), 1)      the light is doubled, in linear terms
///   linear   = light ^ lightgamma
///   lifted   = linear below `knee` ? linear / knee * 0.125
///                                  : 0.125 + (linear - knee) / (1 - knee) * 0.875
///   shown    = lifted ^ (1 / gamma)
///
/// where knee = 0.125 - brightness^2 * 0.075, so brightness 0 leaves the light alone and
/// brightness 1 lifts the dark end. `brightness` is a shader parameter for that reason.
/// Its default of 1, like the gamma of 2.5 above, is what the game ships with: every
/// config.cfg of the nine GoldSrc games checked says `brightness "1.000000"` and
/// `gamma "2.500000"`.
///
/// The curve is applied to the smoothed light rather than to each sample before smoothing,
/// which is not identical but cannot be told apart at this resolution.
String lit_shader_code(bool masked) {
    String code =
        "shader_type spatial;\n"
        "render_mode unshaded;\n"
        "uniform sampler2D albedo_texture : filter_nearest_mipmap, repeat_enable, hint_default_white;\n"
        "uniform sampler2D lightmap_texture : filter_linear, repeat_disable, hint_default_white;\n"
        "uniform float brightness : hint_range(0.0, 1.0) = 1.0;\n"
        "vec3 to_linear(vec3 c) {\n"
        "    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));\n"
        "}\n"
        "vec3 game_light(vec3 stored) {\n"
        "    vec3 f = pow(min(stored * 1.3195, vec3(1.0)), vec3(2.5));\n"
        "    float knee = 0.125 - brightness * brightness * 0.075;\n"
        "    f = mix(f / knee * 0.125, 0.125 + (f - knee) / (1.0 - knee) * 0.875, step(vec3(knee), f));\n"
        "    return pow(f, vec3(0.4));\n"
        "}\n"
        "void fragment() {\n"
        "    vec4 tex = texture(albedo_texture, UV);\n"
        "    vec3 light = game_light(texture(lightmap_texture, UV2).rgb);\n"
        "    ALBEDO = to_linear(clamp(pow(tex.rgb, vec3(0.8)) * light, 0.0, 1.0));\n";
    if (masked) {
        code += "    ALPHA = tex.a;\n"
                "    ALPHA_SCISSOR_THRESHOLD = 0.5;\n";
    }
    code += "}\n";
    return code;
}

} // namespace

MapLighting MapLighting::create(const ir::IRTextureData& lightmap) {
    MapLighting lighting;
    if (!lightmap.is_valid()) return lighting;
    lighting.atlas = TextureConverter::convert(lightmap);
    if (lighting.atlas.is_null()) return lighting;

    lighting.opaque.instantiate();
    lighting.opaque->set_code(lit_shader_code(false));
    lighting.masked.instantiate();
    lighting.masked->set_code(lit_shader_code(true));
    return lighting;
}

Ref<ArrayMesh> MeshConverter::convert(const ir::IRMeshData& ir_mesh, TextureLoader* loader,
                                      const MapLighting* lighting) {
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
        // Bone data is optional, but if present both arrays must cover every vertex.
        if (!surf.bone_indices.empty() && surf.bone_indices.size() != vcount) continue;
        if (!surf.bone_weights.empty() && surf.bone_weights.size() != vcount) continue;

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

        // 6. Skinning (ARRAY_BONES = 10, ARRAY_WEIGHTS = 11)
        // Godot requires both arrays together, at 4 (or 8) entries per vertex. Emitting
        // one without the other, or with a mismatched count, makes the surface fail.
        if (surf.bone_indices.size() == vcount && surf.bone_weights.size() == vcount) {
            PackedInt32Array bones;
            bones.resize(static_cast<int64_t>(vcount) * 4);
            std::memcpy(bones.ptrw(), surf.bone_indices.data(), vcount * 4 * sizeof(int32_t));
            surface_arrays[ArrayMesh::ARRAY_BONES] = bones;

            PackedFloat32Array weights;
            weights.resize(static_cast<int64_t>(vcount) * 4);
            std::memcpy(weights.ptrw(), surf.bone_weights.data(), vcount * 4 * sizeof(float));
            surface_arrays[ArrayMesh::ARRAY_WEIGHTS] = weights;
        }

        // 7. Indices (ARRAY_INDEX = 12)
        // Indices were validated above as < vcount, so every value fits in int32_t and
        // the uint32->int32 reinterpretation is exact. One memcpy beats a scalar loop.
        PackedInt32Array indices;
        indices.resize(static_cast<int64_t>(surf.indices.size()));
        std::memcpy(indices.ptrw(), surf.indices.data(), surf.indices.size() * sizeof(int32_t));
        surface_arrays[ArrayMesh::ARRAY_INDEX] = indices;

        array_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, surface_arrays);

        // Attach the surface's texture. Two sources, in priority order:
        //   1. A texture embedded in the asset itself (GoldSrc .mdl carries its own).
        //   2. IRSurface::material_name resolved through the VFS (BSP lump names, VMT
        //      paths), which needs a loader.
        // Without this step the UVs computed by the parsers have nothing to sample.
        Ref<Texture2D> texture;
        bool masked = !surf.material_name.empty() && surf.material_name.front() == '{';

        if (surf.embedded_texture_index >= 0 &&
            static_cast<size_t>(surf.embedded_texture_index) < ir_mesh.embedded_textures.size()) {
            const auto& embedded = ir_mesh.embedded_textures[surf.embedded_texture_index];
            texture = TextureConverter::convert(embedded);
            masked = masked || embedded.has_alpha;
        } else if (loader && loader->is_valid() && !surf.material_name.empty()) {
            texture = loader->load(surf.material_name);
        }

        if (lighting != nullptr && lighting->is_valid() && !surf.uv1.empty()) {
            // Lit even when its texture was not found: a wall that is missing its WAD then
            // still shows the room's light on plain white, instead of standing out as the
            // one flat, shadowless surface in the level.
            Ref<ShaderMaterial> mat;
            mat.instantiate();
            mat->set_shader(masked ? lighting->masked : lighting->opaque);
            if (texture.is_valid()) {
                mat->set_shader_parameter(kLitAlbedoParameter, texture);
            }
            mat->set_shader_parameter(kLitLightmapParameter, lighting->atlas);
            mat->set_name(String(surf.material_name.c_str()));
            array_mesh->surface_set_material(array_mesh->get_surface_count() - 1, mat);
        } else if (texture.is_valid()) {
            Ref<StandardMaterial3D> mat;
            mat.instantiate();
            mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, texture);
            // Legacy content is authored for point sampling; bilinear filtering blurs
            // 64x64 GoldSrc textures into mush.
            mat->set_texture_filter(BaseMaterial3D::TEXTURE_FILTER_NEAREST_WITH_MIPMAPS);
            // Carry the name the asset used. Anyone opening an imported scene sees which
            // texture a surface came from instead of a numbered material, and it is what
            // makes a wrong-looking surface traceable back to a file.
            mat->set_name(String(surf.material_name.c_str()));

            if (masked) {
                mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
            }

            array_mesh->surface_set_material(array_mesh->get_surface_count() - 1, mat);
        }
    }

    return array_mesh;
}

} // namespace quebratsk::converters
