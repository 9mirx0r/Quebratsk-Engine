#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace quebratsk::ir {

struct IRTerrainData {
    SourceEngine source_engine = SourceEngine::GoldSrc;
    int32_t grid_width = 0;
    int32_t grid_height = 0;
    float cell_size = 1.0f; // Meters per grid cell
    std::vector<float> heightmap; // Grid elevation values

    struct TerrainLayer {
        std::string material_path;
        std::vector<float> weights;
    };
    std::vector<TerrainLayer> layers;

    struct PlacedObject {
        std::string model_path;
        godot::Vector3 position;
        godot::Quaternion rotation;
        float scale = 1.0f;
    };
    std::vector<PlacedObject> objects;
};

} // namespace quebratsk::ir
