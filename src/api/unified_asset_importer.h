#pragma once

#include "../converters/sound_resolver.h"
#include "../core/vfs/vfs_manager.h"
#include "../core/ir/ir_animation_data.h"
#include "../core/ir/ir_skeleton_data.h"
#include "../converters/mesh_converter.h"
#include "../converters/skeleton_converter.h"
#include "../converters/material_converter.h"
#include "../converters/terrain_converter.h"

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/texture2d.hpp>

#include <cstddef>
#include <span>
#include <memory>
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

    /// GoldSrc sidecar animation files, index 0 holding sequence group 1. A model that keeps
    /// its stances in "<name>01.mdl" has almost nothing in the model itself, so without these
    /// its sequence list is full of names that resolve to the bind pose.
    std::vector<std::vector<std::byte>> sequence_groups;

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

    /// Load a model as something that can move under its own power.
    ///
    /// Returns a CharacterBody3D holding the capsule and, beneath it, the same Skeleton3D
    /// that load_model() returns. That is the layout Godot expects of a character: the body
    /// moves, the skeleton is carried along, and a script on the root has move_and_slide()
    /// available without any rearranging.
    ///
    /// load_model() gives a static body instead, which is right for scenery and wrong for
    /// anything meant to walk: a StaticBody3D is an obstacle, not a mover.
    godot::Node3D* load_character(const godot::String& vfs_uri,
                                  const godot::String& pose_name = godot::String(),
                                  const godot::PackedStringArray& animations = godot::PackedStringArray());

    /// Everything the map says is in it, beyond its geometry.
    ///
    /// A .bsp carries a list of entities: where players start, where weapons and ammunition
    /// lie, where the lights are, which model stands where. This is the half of a map that
    /// makes it a place rather than a shape, and until now it was read off disk and thrown
    /// away.
    ///
    /// Returns an Array of Dictionaries, one per entity, with every key the map wrote. The
    /// keys are the game's own, unchanged: "classname", "origin", "angles", and whatever
    /// else that entity type uses. Interpreting them is the caller's business, because what
    /// "info_player_start" means is a property of the game and not of the file format.
    ///
    /// Origins are converted to Godot's axes and metres, so they line up with the geometry
    /// load_map() returns rather than being in the source engine's units.
    godot::Array load_map_entities(const godot::String& vfs_uri);

    /// Load a map as something you can stand on.
    ///
    /// Returns a MeshInstance3D carrying a StaticBody3D with a trimesh collider, which is
    /// the same shape Godot's own "Create Trimesh Static Body" produces, so a user who
    /// later opens the node finds a structure they recognise.
    ///
    /// This exists because load_mesh() hands back geometry and nothing else, and a map with
    /// no collider is a map you fall straight through. Every caller that wanted a usable
    /// map was building the MeshInstance3D by hand and none of them added a body.
    godot::Node3D* load_map(const godot::String& vfs_uri);

    /// VFS URIs of the sounds a named sequence plays, in order.
    ///
    /// A weapon's firing animation names its own gunshot, a footstep animation names the
    /// footstep. This is the only place in these files that ties an action to a noise: a
    /// .mdl says nothing else about it, so any other way of choosing a sound for a weapon
    /// is choosing one at random.
    ///
    /// The names in the model are followed to real files before they are returned, because
    /// they are not files as written: Source names a soundscript entry, "Weapon_357.Single".
    /// Anything that cannot be found is left out and reported.
    ///
    /// Pass an empty `sequence` to get every sound the model can make.
    godot::PackedStringArray list_sounds(const godot::String& vfs_uri,
                                         const godot::String& sequence = godot::String());

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
    ///
    /// `origin_uri` is where the asset came from. Material references in a model are bare
    /// fragments that only a search can answer, and the search needs to know which game is
    /// asking or it will answer from whichever one it reaches first.
    godot::Node3D* build_model_node(const ParsedAssetIR& parsed,
                                    const godot::String& pose_name = godot::String(),
                                    const godot::String& origin_uri = godot::String());

private:
    /// Hang an AnimationPlayer carrying every decoded sequence off the skeleton.
    /// Does nothing when no animations were requested, which is the default.
    static void attach_animations(godot::Skeleton3D* skeleton, const ParsedAssetIR& parsed);

    /// A StaticBody3D holding a trimesh collider for `mesh`, to be parented to whatever
    /// draws it. Trimesh rather than convex because a map is not remotely convex: a hull
    /// around a building fills in its rooms.
    static godot::StaticBody3D* trimesh_body(const godot::Ref<godot::Mesh>& mesh);

    /// A StaticBody3D holding a capsule sized to `bounds`, for a character.
    ///
    /// A skinned mesh cannot take a trimesh collider that means anything: the collider
    /// would be frozen in the rest pose while the mesh moves away from it. A capsule is
    /// what a character is treated as by every engine that has to answer "did the bullet
    /// hit them", and it stays right through any animation.
    static godot::StaticBody3D* capsule_body(const godot::AABB& bounds);

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

    /// Kept rather than made per call: building it reads every soundscript the mounted
    /// games ship, which is dozens of files, and a scene asking about twenty weapons would
    /// otherwise pay for that twenty times.
    std::unique_ptr<converters::SoundResolver> m_sounds;
    mutable godot::PackedStringArray m_last_missing;
};

} // namespace quebratsk::api
