#include "batch_gltf_converter.h"
#include <filesystem>

namespace quebratsk::converters {

BatchExportResult BatchGLTFConverter::export_vfs_container_to_gltf(
    vfs::VFSManager* vfs,
    const std::string& vfs_prefix,
    const std::string& output_directory
) {
    BatchExportResult result;
    if (!vfs) return result;

    if (!std::filesystem::exists(output_directory)) {
        std::filesystem::create_directories(output_directory);
    }

    return result;
}

} // namespace quebratsk::converters
