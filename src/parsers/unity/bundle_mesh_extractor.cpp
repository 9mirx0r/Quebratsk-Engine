#include "bundle_mesh_extractor.h"

namespace quebratsk::parsers::unity {

using namespace godot;

void BundleMeshExtractor::_bind_methods() {
    // ClassDB bindings for helper static methods if needed
}

ir::IRMeshData BundleMeshExtractor::extract_mesh_ir(
    std::span<const std::byte> mesh_bytes
) {
    ir::IRMeshData ir_mesh;
    ir_mesh.source_engine = ir::SourceEngine::Enfusion; // Unity namespace mapping
    ir_mesh.name = "Unity_SerializedMesh";

    if (mesh_bytes.empty()) return ir_mesh;

    // Unity Serialized 3D Mesh Format decoding stub
    ir::IRSurface surface;
    surface.material_name = "Unity_DefaultMaterial";
    ir_mesh.surfaces.push_back(std::move(surface));

    return ir_mesh;
}

} // namespace quebratsk::parsers::unity
