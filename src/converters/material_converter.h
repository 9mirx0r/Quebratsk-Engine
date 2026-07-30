#pragma once

#include "../core/ir/ir_material_data.h"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

namespace quebratsk::converters {

class TextureLoader;

class MaterialConverter {
public:
    /// Convert IRMaterialData into a native Godot 4 StandardMaterial3D.
    ///
    /// When `loader` is non-null the IR's texture references are resolved through the
    /// VFS and bound to the material. Passing null keeps the previous behaviour
    /// (colour/roughness/metallic only), which is what produced untextured imports.
    ///
    /// MAIN THREAD ONLY when `loader` is provided.
    [[nodiscard]] static godot::Ref<godot::StandardMaterial3D> convert(
        const ir::IRMaterialData& ir_mat,
        TextureLoader* loader = nullptr);
};

} // namespace quebratsk::converters
