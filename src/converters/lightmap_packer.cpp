#include "lightmap_packer.h"

namespace quebratsk::converters {

void LightmapPacker::generate_uv2_lightmap_atlas(ir::IRMeshData& ir_mesh, uint32_t atlas_size) {
    for (auto& surf : ir_mesh.surfaces) {
        if (surf.positions.empty()) continue;

        surf.uv1.resize(surf.positions.size());
        for (size_t i = 0; i < surf.positions.size(); ++i) {
            float u = (surf.positions[i].x + 10.0f) / static_cast<float>(atlas_size);
            float v = (surf.positions[i].z + 10.0f) / static_cast<float>(atlas_size);
            surf.uv1[i] = godot::Vector2(u, v);
        }
    }
}

} // namespace quebratsk::converters
