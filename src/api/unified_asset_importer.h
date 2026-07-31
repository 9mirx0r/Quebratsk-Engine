#pragma once

#include "../core/vfs/vfs_manager.h"
#include "../core/ir/ir_animation_data.h"
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

    /// Fully decoded sequences, one per name the caller asked for. Empty otherwise:
    /// nothing decodes an animation unless it was requested by name.
    std::vector<ir::IRAnimationData> animations;
};

/// An asset's bytes plus any companion files it cannot be decoded without.
///
/// Some formats are split across several files — a Source .mdl holds no vertex data at
/// all; positions live in a .vvd and the index data in a .dx90.vtx. Resolving those
/// siblings needs the VFS, which is main-thread-owned, so the read happens up front and
/// the parser receives plain buffers. That keeps parse_asset_ir() pure and off-thread.
/// A .mdl fetched for its animation, together with the external block file it defers
/// its long sequences to.
struct IncludedModelBytes {
    std::vector<std::byte> mdl;
    std::vector<std::byte> ani;
};

struct AssetBundleBytes {
    std::vector<std::byte> primary;   // the asset named by the URI
    std::vector<std::byte> vertices;  // Source .vvd, empty when absent or not applicable
    std::vector<std::byte> indices;   // Source .dx90.vtx, likewise
    std::vector<std::byte> textures;  // GoldSrc "<name>T.mdl", likewise
    std::vector<std::byte> animations; // Source .ani animation blocks, likewise

    /// Source animation models named by the .mdl's includemodel table. Their paths are
    /// only known after reading the header, so they are fetched in a second pass.
    std::vector<IncludedModelBytes> include_models;

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
    ///
    /// `with_geometry = false` skips the .vvd and .vtx. They carry only vertex and index
    /// data and are the bulk of a model's bytes, so a caller that only wants the skeleton
    /// or the pose list should not pay to read them.
    [[nodiscard]] AssetBundleBytes read_asset_bundle(const godot::String& vfs_uri,
                                                     bool with_geometry = true) const;

    /// Load a complete model: geometry, skeleton and skin, wired together.
    ///
    /// Returns a Skeleton3D with a skinned MeshInstance3D child when the asset has
    /// bones, otherwise a bare MeshInstance3D. The node is unparented and owned by the
    /// caller — add it to the tree or free it.
    ///
    /// `pose_name` selects one of the model's animation sequences to stand in, by exact
    /// label first and then as a substring ("idle_smg1", "crouch"). Left empty, an
    /// idle-like sequence is chosen automatically. Either way the full list of labels is
    /// published on the returned node as the "quebratsk_poses" metadata.
    ///
    /// `animations` names sequences to import as playable animation rather than as a
    /// single frozen frame. Each becomes an Animation on an AnimationPlayer added to the
    /// returned node, under the label the game uses. Naming sequences costs real time and
    /// memory — a 60-frame sequence is 60 keyframes per bone — so nothing is decoded
    /// unless it is asked for, and list_poses() is how a caller learns what to ask for.
    godot::Node3D* load_model(const godot::String& vfs_uri,
                              const godot::String& pose_name = godot::String(),
                              const godot::PackedStringArray& animations = godot::PackedStringArray());

    /// Every animation sequence label the model carries, e.g. "idle_smg1".
    ///
    /// Skips the .vvd and .vtx entirely — poses live in the .mdl and its .ani, so the
    /// vertex and index data (by far the bulk of a model) is never read or decoded.
    /// That is what makes this cheap enough to drive an inspector dropdown.
    godot::PackedStringArray list_poses(const godot::String& vfs_uri);

    /// Build the scene graph for an already-parsed model. Main thread only: this is the
    /// half of load_model() that allocates Godot Objects.
    ///
    /// Split out so AsyncAssetImporter can parse on a worker and construct here, instead
    /// of re-reading and re-parsing the asset on the main thread.
    godot::Node3D* build_model_node(const ParsedAssetIR& parsed,
                                    const godot::String& pose_name = godot::String());

private:
    /// Hang an AnimationPlayer carrying every decoded sequence off the skeleton.
    /// Does nothing when no animations were requested, which is the default.
    static void attach_animations(godot::Skeleton3D* skeleton, const ParsedAssetIR& parsed);

public:

    /// Why the last load_* call failed. Set by every load_* entry point, on both the
    /// success and the failure paths, so it always describes the most recent call.
    enum ErrorCode : int {
        ERR_OK = 0,
        ERR_VFS_NOT_SET = 1,       // set_vfs() was never called
        ERR_ASSET_UNREADABLE = 2,  // URI not in the VFS, or the read failed
        ERR_PARSE_FAILED = 3,      // decoded to nothing usable
    };

    int get_last_error_code() const { return m_last_error_code; }

    /// Companion files the last load looked for and did not find, by filename.
    ///
    /// A Source .mdl carries no vertex data at all: it lives in a .vvd and a .vtx beside
    /// it. When those are in an archive the user has not mounted, the import fails with
    /// ERR_PARSE_FAILED and no way to tell what is wrong. Naming the file turns that dead
    /// end into something the caller can act on, or offer to fix.
    ///
    /// Empty when the last load needed no companions or found them all.
    godot::PackedStringArray get_last_missing_companions() const { return m_last_missing; }

    /// Parse raw asset bytes into the engine-agnostic IR.
    /// Pure data in, pure data out: allocates no Godot Object/Resource and touches
    /// no server, so it is safe to call from any thread. AsyncAssetImporter relies
    /// on this guarantee — do not introduce Ref<> or memnew() into this path.
    ///
    /// With `pose_names_only`, the returned poses carry their names but no transforms.
    /// Only list_poses() wants that; anything that has to *stand* the model in a pose
    /// must leave it false.
    [[nodiscard]] static ParsedAssetIR parse_asset_ir(const AssetBundleBytes& bundle,
                                                      const std::string& lowercase_uri,
                                                      bool pose_names_only = false,
                                                      const std::vector<std::string>& animate = {});

    [[nodiscard]] vfs::VFSManager* get_vfs() const { return m_vfs; }

private:
    vfs::VFSManager* m_vfs = nullptr;
    mutable int m_last_error_code = 0;
    mutable godot::PackedStringArray m_last_missing;
};

} // namespace quebratsk::api
