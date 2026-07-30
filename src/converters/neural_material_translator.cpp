#include "neural_material_translator.h"
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string>

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

    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    auto contains_any = [](const std::string& hay, std::initializer_list<const char*> needles) {
        return std::any_of(needles.begin(), needles.end(),
                           [&hay](const char* n) { return hay.find(n) != std::string::npos; });
    };

    // Without normalization "Metal" never matched, so behaviour depended on the
    // asset author's capitalization.
    const std::string mat_str = lower(material_name.utf8().get_data());
    const std::string type_str = lower(shader_type.utf8().get_data());

    // Heuristic material translation rules.
    // NOTE: VertexLitGeneric is deliberately NOT treated as metal. It is Source's
    // *default* model shader — wood, cloth, skin and plastic all use it — so the
    // previous rule made the majority of imported props look chrome-plated.
    if (contains_any(mat_str, {"metal", "steel", "chrome", "iron", "alum", "brass", "copper"})) {
        mat->set_metallic(0.85f);
        mat->set_roughness(0.25f);
    } else if (contains_any(mat_str, {"water", "glass"})) {
        mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        mat->set_albedo(Color(1.0f, 1.0f, 1.0f, 0.6f));
        mat->set_roughness(0.05f);
        mat->set_metallic(0.10f);
    } else if (contains_any(mat_str, {"concrete", "stone", "brick", "rock", "gravel"})) {
        mat->set_metallic(0.0f);
        mat->set_roughness(0.90f);
    } else if (contains_any(mat_str, {"wood", "plank", "cloth", "fabric", "dirt", "sand"})) {
        mat->set_metallic(0.0f);
        mat->set_roughness(0.80f);
    } else {
        mat->set_roughness(default_roughness);
        mat->set_metallic(default_metallic);
    }

    return mat;
}

} // namespace quebratsk::converters
