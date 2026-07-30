#pragma once

#include "../core/vfs/vfs_manager.h"
#include "../converters/mesh_converter.h"
#include "../converters/skeleton_converter.h"
#include "../converters/material_converter.h"
#include "../converters/terrain_converter.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

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

    /// Copy an asset's decompressed bytes out of the VFS into an owned buffer.
    /// Must run on the thread that owns the VFSManager (the main thread).
    /// Returns an empty vector if the URI is missing or unreadable.
    [[nodiscard]] std::vector<std::byte> read_asset_bytes(const godot::String& vfs_uri) const;

    /// Parse raw asset bytes into the engine-agnostic IR.
    /// Pure data in, pure data out: allocates no Godot Object/Resource and touches
    /// no server, so it is safe to call from any thread. AsyncAssetImporter relies
    /// on this guarantee — do not introduce Ref<> or memnew() into this path.
    [[nodiscard]] static ir::IRMeshData parse_mesh_ir(std::span<const std::byte> data,
                                                     const std::string& lowercase_uri);

    [[nodiscard]] vfs::VFSManager* get_vfs() const { return m_vfs; }

private:
    vfs::VFSManager* m_vfs = nullptr;
};

} // namespace quebratsk::api
