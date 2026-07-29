#pragma once

#include "../core/ir/ir_mesh_data.h"

#include <vector>

namespace quebratsk::converters {

struct LODMeshLevel {
    uint32_t level = 0; // 1, 2, 3
    float reduction_ratio = 0.5f; // 50% simplification
    ir::IRMeshData mesh_data;
};

class LODGenerator {
public:
    /// Generate automated low-poly LOD levels from high-poly IRMeshData
    [[nodiscard]] static std::vector<LODMeshLevel> generate_lods(
        const ir::IRMeshData& high_poly_mesh,
        uint32_t lod_count = 3
    );
};

} // namespace quebratsk::converters
