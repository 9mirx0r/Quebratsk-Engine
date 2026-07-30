#pragma once

#include "../core/ir/ir_mesh_data.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/ref.hpp>

namespace quebratsk::converters {

class TextureLoader;

class MeshConverter {
public:
    /// Convert IRMeshData into a native Godot 4 ArrayMesh.
    ///
    /// When `loader` is non-null, each surface's `material_name` is resolved through the
    /// VFS and attached as a StandardMaterial3D. Passing null yields an untextured mesh.
    ///
    /// MAIN THREAD ONLY when `loader` is provided.
    [[nodiscard]] static godot::Ref<godot::ArrayMesh> convert(const ir::IRMeshData& ir_mesh,
                                                              TextureLoader* loader = nullptr);
};

} // namespace quebratsk::converters
