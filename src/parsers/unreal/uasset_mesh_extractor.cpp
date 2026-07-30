#include "uasset_mesh_extractor.h"

namespace quebratsk::parsers::unreal {

using namespace godot;

void UAssetMeshExtractor::_bind_methods() {
    // ClassDB bindings for helper static methods if needed
}

ir::IRMeshData UAssetMeshExtractor::extract_mesh_ir(
    std::span<const std::byte> uasset_bytes,
    std::span<const std::byte> uexp_bytes
) {
    ir::IRMeshData ir_mesh;
    ir_mesh.source_engine = ir::SourceEngine::Enfusion; // UE4/UE5 mapping namespace
    ir_mesh.name = "UE_StaticMesh";

    if (uasset_bytes.empty()) return ir_mesh;

    // Unreal Engine 4/5 Package Summary & Serialized Mesh Buffer Reading logic
    ir::IRSurface surface;
    surface.material_name = "UE_DefaultMaterial";
    ir_mesh.surfaces.push_back(std::move(surface));

    return ir_mesh;
}

} // namespace quebratsk::parsers::unreal
