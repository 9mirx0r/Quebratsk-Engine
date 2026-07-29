#pragma once

#include "../core/ir/ir_mesh_data.h"

#include <vector>

namespace quebratsk::converters {

struct LightmapRect {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t w = 0;
    uint32_t h = 0;
};

class LightmapPacker {
public:
    /// Compute UV2 secondary lightmap coordinates for ir_mesh surfaces using 2D bin packing
    static void generate_uv2_lightmap_atlas(ir::IRMeshData& ir_mesh, uint32_t atlas_size = 1024);
};

} // namespace quebratsk::converters
