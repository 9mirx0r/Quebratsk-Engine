#include "material_converter.h"
#include "texture_loader.h"

#include <godot_cpp/classes/texture2d.hpp>

namespace quebratsk::converters {

using namespace godot;

Ref<StandardMaterial3D> MaterialConverter::convert(const ir::IRMaterialData& ir_mat,
                                                   TextureLoader* loader) {
    Ref<StandardMaterial3D> mat;
    mat.instantiate();

    mat->set_albedo(ir_mat.albedo_color);
    mat->set_metallic(ir_mat.metallic);
    mat->set_roughness(ir_mat.roughness);

    if (ir_mat.is_transparent) {
        mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
    }

    if (ir_mat.is_two_sided) {
        mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
    }

    if (ir_mat.is_additive) {
        mat->set_blend_mode(BaseMaterial3D::BLEND_MODE_ADD);
    }

    // Bind the textures the parsers have been recording all along. Before this, the
    // IR's texture references were written by VMTParser and read by nobody, so every
    // imported material came out flat-coloured.
    if (loader && loader->is_valid()) {
        if (ir_mat.albedo_texture) {
            if (Ref<Texture2D> tex = loader->load(ir_mat.albedo_texture->vfs_path); tex.is_valid()) {
                mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
            }
        }
        if (ir_mat.normal_texture) {
            if (Ref<Texture2D> tex = loader->load(ir_mat.normal_texture->vfs_path); tex.is_valid()) {
                mat->set_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING, true);
                mat->set_texture(BaseMaterial3D::TEXTURE_NORMAL, tex);
                mat->set_normal_scale(ir_mat.normal_scale);
            }
        }
        if (ir_mat.emission_texture) {
            if (Ref<Texture2D> tex = loader->load(ir_mat.emission_texture->vfs_path); tex.is_valid()) {
                mat->set_feature(BaseMaterial3D::FEATURE_EMISSION, true);
                mat->set_texture(BaseMaterial3D::TEXTURE_EMISSION, tex);
            }
        }
    }

    return mat;
}

} // namespace quebratsk::converters
