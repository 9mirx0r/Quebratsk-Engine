#pragma once

#include "../core/ir/ir_mesh_data.h"
#include "../core/ir/ir_skeleton_data.h"

#include <string>

namespace quebratsk::converters {

class GLTFExporter {
public:
    /// Export IRMeshData and IRSkeletonData directly to a standard .gltf / .glb 3D asset file
    [[nodiscard]] static bool export_to_gltf(
        const ir::IRMeshData& ir_mesh,
        const ir::IRSkeletonData& ir_skeleton,
        const std::string& output_file_path
    );
};

} // namespace quebratsk::converters
