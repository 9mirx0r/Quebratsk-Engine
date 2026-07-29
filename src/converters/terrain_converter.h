#pragma once

#include "../core/ir/ir_terrain_data.h"

#include <godot_cpp/classes/height_map_shape3d.hpp>
#include <godot_cpp/classes/ref.hpp>

namespace quebratsk::converters {

class TerrainConverter {
public:
    /// Convert Intermediate Representation IRTerrainData into native Godot 4 HeightMapShape3D
    [[nodiscard]] static godot::Ref<godot::HeightMapShape3D> convert(const ir::IRTerrainData& ir_terrain);
};

} // namespace quebratsk::converters
