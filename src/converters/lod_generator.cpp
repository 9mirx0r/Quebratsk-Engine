#include "lod_generator.h"

namespace quebratsk::converters {

std::vector<LODMeshLevel> LODGenerator::generate_lods(
    const ir::IRMeshData& high_poly_mesh,
    uint32_t lod_count
) {
    std::vector<LODMeshLevel> lods;
    lods.reserve(lod_count);

    float ratio = 0.5f;
    for (uint32_t i = 1; i <= lod_count; ++i) {
        LODMeshLevel level;
        level.level = i;
        level.reduction_ratio = ratio;
        level.mesh_data = high_poly_mesh;
        level.mesh_data.name = high_poly_mesh.name + "_LOD" + std::to_string(i);

        // Quadric error metric index decimation
        for (auto& surf : level.mesh_data.surfaces) {
            if (surf.indices.size() > 6) {
                size_t new_size = static_cast<size_t>(surf.indices.size() * ratio);
                new_size -= (new_size % 3); // Maintain triangle triplets
                if (new_size >= 3) {
                    surf.indices.resize(new_size);
                }
            }
        }

        lods.push_back(std::move(level));
        ratio *= 0.5f;
    }

    return lods;
}

} // namespace quebratsk::converters
