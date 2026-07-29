#pragma once

#include "../core/vfs/vfs_manager.h"
#include "gltf_exporter.h"

#include <string>
#include <vector>

namespace quebratsk::converters {

struct BatchExportResult {
    uint32_t total_exported = 0;
    uint32_t failed_count = 0;
    std::vector<std::string> exported_paths;
};

class BatchGLTFConverter {
public:
    /// Batch convert all 3D meshes in a mounted VFS container into standalone .gltf files
    [[nodiscard]] static BatchExportResult export_vfs_container_to_gltf(
        vfs::VFSManager* vfs,
        const std::string& vfs_prefix,
        const std::string& output_directory
    );
};

} // namespace quebratsk::converters
