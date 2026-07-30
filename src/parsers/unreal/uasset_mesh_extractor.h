#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>
#include "../../core/ir/ir_mesh_data.h"
#include <span>
#include <vector>

namespace quebratsk::parsers::unreal {

class UAssetMeshExtractor : public godot::Object {
    GDCLASS(UAssetMeshExtractor, godot::Object)

protected:
    static void _bind_methods();

public:
    UAssetMeshExtractor() = default;
    ~UAssetMeshExtractor() = default;

    /// Extract 3D StaticMesh IR data from Unreal Engine 4/5 .uasset and .uexp byte buffers
    static quebratsk::ir::IRMeshData extract_mesh_ir(
        std::span<const std::byte> uasset_bytes,
        std::span<const std::byte> uexp_bytes
    );
};

} // namespace quebratsk::parsers::unreal
