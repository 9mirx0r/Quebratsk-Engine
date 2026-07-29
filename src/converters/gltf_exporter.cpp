#include "gltf_exporter.h"
#include <fstream>

namespace quebratsk::converters {

bool GLTFExporter::export_to_gltf(
    const ir::IRMeshData& ir_mesh,
    const ir::IRSkeletonData& ir_skeleton,
    const std::string& output_file_path
) {
    std::ofstream out(output_file_path);
    if (!out.is_open()) return false;

    out << "{\n";
    out << "  \"asset\": { \"version\": \"2.0\", \"generator\": \"QuebratskEngine C++20\" },\n";
    out << "  \"scene\": 0,\n";
    out << "  \"scenes\": [ { \"name\": \"" << ir_mesh.name << "\", \"nodes\": [ 0 ] } ]\n";
    out << "}\n";

    return true;
}

} // namespace quebratsk::converters
