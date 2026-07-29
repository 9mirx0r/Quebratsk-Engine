#pragma once

#include "../core/ir/ir_material_data.h"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/shader_material.hpp>

namespace quebratsk::converters {

class ShaderBridge {
public:
    /// Create custom Godot ShaderMaterial for GoldSrc water, glass transparency, or Source $selfillum
    [[nodiscard]] static godot::Ref<godot::ShaderMaterial> create_specialized_material(
        const ir::IRMaterialData& ir_material
    );
};

} // namespace quebratsk::converters
