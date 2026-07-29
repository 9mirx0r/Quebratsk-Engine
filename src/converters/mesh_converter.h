#pragma once

#include "../core/ir/ir_mesh_data.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/ref.hpp>

namespace quebratsk::converters {

class MeshConverter {
public:
    /// Convert Intermediate Representation IRMeshData into native Godot 4 ArrayMesh
    [[nodiscard]] static godot::Ref<godot::ArrayMesh> convert(const ir::IRMeshData& ir_mesh);
};

} // namespace quebratsk::converters
