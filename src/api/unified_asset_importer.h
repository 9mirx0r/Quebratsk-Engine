#pragma once

#include "../core/vfs/vfs_manager.h"
#include "../core/ir/ir_skeleton_data.h"
#include "../converters/mesh_converter.h"
#include "../converters/skeleton_converter.h"
#include "../converters/material_converter.h"
#include "../converters/terrain_converter.h"

#include <godot_cpp/classes/node3d.hpp>

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/texture2d.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace quebratsk::api {

/// Everything a parser recovers from one asset, still engine-agnostic.
struct ParsedAssetIR {
    ir::IRMeshData mesh;
    ir::IRSkeletonData skeleton;
};

/// An asset's bytes plus any companion files it cannot be decoded without.
///
/// Some formats are split across several files — a Source .mdl holds no vertex data at
/// all; positions live in a .vvd and the index data in a .dx90.vtx. Resolving those
/// siblings needs the VFS, which is main-thread-owned, so the read happens up front and
/// the parser receives plain buffers. That keeps parse_asset_ir() pure and off-thread.
struct AssetBundleBytes {
    std::vector<std::byte> primary;   // the asset named by the URI
    std::vector<std::byte> vertices;  // Source .vvd, empty when absent or not applicable
    std::vector<std::byte> indices;   // Source .dx90.vtx, likewise

    [[nodiscard]] bool empty() const { return primary.empty(); }
};

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

    /// Resolve and decode a legacy texture reference ("metal/metalwall001a", a WAD3
    /// lump name, or a full VFS URI) into a Godot texture.
    godot::Ref<godot::Texture2D> load_texture(const godot::String& texture_ref);

    /// Copy an asset's decompressed bytes out of the VFS into an owned buffer.
    /// Must run on the thread that owns the VFSManager (the main thread).
    /// Returns an empty vector if the URI is missing or unreadable.
    [[nodiscard]] std::vector<std::byte> read_asset_bytes(const godot::String& vfs_uri) const;

    /// Read an asset together with the companion files its format requires.
    /// Must run on the thread that owns the VFSManager (the main thread).
    [[nodiscard]] AssetBundleBytes read_asset_bundle(const godot::String& vfs_uri) const;

    /// Load a complete model: geometry, skeleton and skin, wired together.
    ///
    /// Returns a Skeleton3D with a skinned MeshInstance3D child when the asset has
    /// bones, otherwise a bare MeshInstance3D. The node is unparented and owned by the
    /// caller — add it to the tree or free it.
    godot::Node3D* load_model(const godot::String& vfs_uri);

    /// Parse raw asset bytes into the engine-agnostic IR.
    /// Pure data in, pure data out: allocates no Godot Object/Resource and touches
    /// no server, so it is safe to call from any thread. AsyncAssetImporter relies
    /// on this guarantee — do not introduce Ref<> or memnew() into this path.
    [[nodiscard]] static ParsedAssetIR parse_asset_ir(const AssetBundleBytes& bundle,
                                                      const std::string& lowercase_uri);

    [[nodiscard]] vfs::VFSManager* get_vfs() const { return m_vfs; }

private:
    vfs::VFSManager* m_vfs = nullptr;
};

} // namespace quebratsk::api
