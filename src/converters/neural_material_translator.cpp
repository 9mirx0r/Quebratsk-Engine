#include "neural_material_translator.h"
#include <godot_cpp/variant/utility_functions.hpp>

namespace quebratsk::converters {

using namespace godot;

void NeuralMaterialTranslator::_bind_methods() {
    ClassDB::bind_static_method("NeuralMaterialTranslator", D_METHOD("translate_material", "material_name", "shader_type", "default_roughness", "default_metallic"), &NeuralMaterialTranslator::translate_material, DEFVAL(0.5f), DEFVAL(0.0f));
}

Ref<StandardMaterial3D> NeuralMaterialTranslator::translate_material(
    const String& material_name,
    const String& shader_type,
    float default_roughness,
    float default_metallic
) {
    Ref<StandardMaterial3D> mat;
    mat.instantiate();

    std::string mat_str = material_name.utf8().get_data();
    std::string type_str = shader_type.utf8().get_data();

    // Heuristic neural material translation rules
    if (mat_str.find("metal") != std::string::npos || mat_str.find("steel") != std::string::npos || type_str == "VertexLitGeneric") {
        mat->set_metallic(0.85f);
        mat->set_roughness(0.25f);
    } else if (mat_str.find("water") != std::string::npos || mat_str.find("glass") != std::string::npos) {
        mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        mat->set_albedo(Color(1.0f, 1.0f, 1.0f, 0.6f));
        mat->set_roughness(0.05f);
        mat->set_metallic(0.10f);
    } else if (mat_str.find("concrete") != std::string::npos || mat_str.find("stone") != std::string::npos) {
        mat->set_metallic(0.0f);
        mat->set_roughness(0.90f);
    } else {
        mat->set_roughness(default_roughness);
        mat->set_metallic(default_metallic);
    }

    return mat;
}

} // namespace quebratsk::converters
