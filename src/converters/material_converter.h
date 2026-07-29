#pragma once

#include "../core/ir/ir_material_data.h"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

namespace quebratsk::converters {

class MaterialConverter {
public:
    /// Convert Intermediate Representation IRMaterialData into native Godot 4 StandardMaterial3D
    [[nodiscard]] static godot::Ref<godot::StandardMaterial3D> convert(const ir::IRMaterialData& ir_mat);
};

} // namespace quebratsk::converters
