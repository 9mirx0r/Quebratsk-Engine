#include "terrain_converter.h"
#include <godot_cpp/variant/packed_float32_array.hpp>

namespace quebratsk::converters {

using namespace godot;

Ref<HeightMapShape3D> TerrainConverter::convert(const ir::IRTerrainData& ir_terrain) {
    Ref<HeightMapShape3D> shape;
    shape.instantiate();

    shape->set_map_width(ir_terrain.grid_width);
    shape->set_map_depth(ir_terrain.grid_height);

    PackedFloat32Array heights;
    heights.resize(static_cast<int64_t>(ir_terrain.heightmap.size()));

    for (size_t i = 0; i < ir_terrain.heightmap.size(); ++i) {
        heights.set(static_cast<int64_t>(i), ir_terrain.heightmap[i]);
    }

    shape->set_map_data(heights);
    return shape;
}

} // namespace quebratsk::converters
