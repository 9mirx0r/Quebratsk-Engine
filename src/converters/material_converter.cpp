#include "material_converter.h"

namespace quebratsk::converters {

using namespace godot;

Ref<StandardMaterial3D> MaterialConverter::convert(const ir::IRMaterialData& ir_mat) {
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

    return mat;
}

} // namespace quebratsk::converters
