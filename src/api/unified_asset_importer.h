#pragma once

#include "../core/vfs/vfs_manager.h"
#include "../converters/mesh_converter.h"
#include "../converters/skeleton_converter.h"
#include "../converters/material_converter.h"
#include "../converters/terrain_converter.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>

namespace quebratsk::api {

class UnifiedAssetImporter : public godot::Node {
    GDCLASS(UnifiedAssetImporter, godot::Node);

protected:
    static void _bind_methods();

public:
    UnifiedAssetImporter() = default;
    ~UnifiedAssetImporter() override = default;

    /// Set VFS Manager instance
    void set_vfs(vfs::VFSManager* vfs);

    /// Load 3D mesh asset from VFS URI (.mdl, .p3d, .bsp) as native Godot ArrayMesh
    godot::Ref<godot::ArrayMesh> load_mesh(const godot::String& vfs_uri);

    /// Load material asset from VFS URI (.vmt, .rvmat) as native StandardMaterial3D
    godot::Ref<godot::StandardMaterial3D> load_material(const godot::String& vfs_uri);

    /// Load terrain heightmap from VFS URI (.wrp) as native HeightMapShape3D
    godot::Ref<godot::HeightMapShape3D> load_terrain(const godot::String& vfs_uri);

private:
    vfs::VFSManager* m_vfs = nullptr;
};

} // namespace quebratsk::api
