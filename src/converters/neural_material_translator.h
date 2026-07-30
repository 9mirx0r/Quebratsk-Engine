#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <string>

namespace quebratsk::converters {

/// AI / Heuristic Material Translator converting legacy VMT, RVMAT & WAD3 materials into modern PBR StandardMaterial3D
class NeuralMaterialTranslator : public godot::Object {
    GDCLASS(NeuralMaterialTranslator, godot::Object)

protected:
    static void _bind_methods();

public:
    NeuralMaterialTranslator() = default;
    ~NeuralMaterialTranslator() override = default;

    /// Analyzes legacy material parameters and generates PBR StandardMaterial3D with Roughness/Metallic/AO
    static godot::Ref<godot::StandardMaterial3D> translate_material(
        const godot::String& material_name,
        const godot::String& shader_type,
        float default_roughness = 0.5f,
        float default_metallic = 0.0f
    );
};

} // namespace quebratsk::converters
