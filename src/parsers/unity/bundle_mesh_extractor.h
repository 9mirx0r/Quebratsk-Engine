#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>
#include "../../core/ir/ir_mesh_data.h"
#include <span>
#include <vector>

namespace quebratsk::parsers::unity {

class BundleMeshExtractor : public godot::Object {
    GDCLASS(BundleMeshExtractor, godot::Object)

protected:
    static void _bind_methods();

public:
    BundleMeshExtractor() = default;
    ~BundleMeshExtractor() = default;

    /// Extract Unity 3D Serialized Mesh IR data from .bundle byte stream
    static quebratsk::ir::IRMeshData extract_mesh_ir(
        std::span<const std::byte> mesh_bytes
    );
};

} // namespace quebratsk::parsers::unity
