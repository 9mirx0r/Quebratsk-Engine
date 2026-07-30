#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>

namespace quebratsk::parsers::source1 {

/// NOTE: GoldSrc StudioMDL v10 uses this same magic. Only the version field
/// distinguishes the two formats, so any dispatcher must check it.
inline constexpr std::array<char, 4> kSourceMdlMagic = {'I', 'D', 'S', 'T'};
inline constexpr int32_t kSourceMdlMinVersion = 44;
inline constexpr int32_t kSourceMdlMaxVersion = 49;

#pragma pack(push, 1)
/// Header of Source 1 StudioMDL v44-49 file (408+ bytes)
struct SourceStudioHeader {
    char magic[4];          // "IDST"
    int32_t version;        // 44, 48, 49
    int32_t checksum;
    char name[64];
    int32_t length;

    float eyeposition[3];
    float illumposition[3];
    float hull_min[3], hull_max[3];
    float view_bbmin[3], view_bbmax[3];

    int32_t flags;

    int32_t num_bones;
    int32_t bone_index;

    int32_t num_bone_controllers;
    int32_t bone_controller_index;

    int32_t num_hitbox_sets;
    int32_t hitbox_set_index;

    int32_t num_local_anim;
    int32_t local_anim_index;

    int32_t num_local_seq;
    int32_t local_seq_index;

    int32_t activity_list_version;
    int32_t events_indexed;

    int32_t num_textures;
    int32_t texture_index;

    int32_t num_cdtextures;
    int32_t cdtexture_index;

    // These three were missing, so num_bodyparts and bodypart_index below were reading
    // num_skin_ref and num_skin_families instead — every body-part traversal would have
    // walked garbage offsets.
    int32_t num_skin_ref;
    int32_t num_skin_families;
    int32_t skin_index;

    int32_t num_bodyparts;
    int32_t bodypart_index;

    // Everything from here to include_model_index exists only so that field lands at
    // its real offset (340). GMod player models carry no animation of their own — they
    // borrow it from a shared base model named here — so this is the field that decides
    // whether a character can be posed at all.
    int32_t num_attachments;
    int32_t attachment_index;
    int32_t num_local_nodes;
    int32_t local_node_index;
    int32_t local_node_name_index;
    int32_t num_flex_desc;
    int32_t flex_desc_index;
    int32_t num_flex_controllers;
    int32_t flex_controller_index;
    int32_t num_flex_rules;
    int32_t flex_rule_index;
    int32_t num_ik_chains;
    int32_t ik_chain_index;
    int32_t num_mouths;
    int32_t mouth_index;
    int32_t num_local_pose_parameters;
    int32_t local_pose_param_index;
    int32_t surface_prop_index;
    int32_t key_value_index;
    int32_t key_value_size;
    int32_t num_local_ik_autoplay_locks;
    int32_t local_ik_autoplay_lock_index;
    float mass;
    int32_t contents;

    int32_t num_include_models;
    int32_t include_model_index;

    // Long animations are not stored in the .mdl at all. studiomdl splits them into a
    // companion .ani named here, and every animation descriptor whose anim_block is
    // non-zero indexes into that file instead. m_anm.mdl is 0.9 MB against a 7.1 MB
    // m_anm.ani, so without these fields almost every real stance is unreachable.
    int32_t virtual_model;          // runtime pointer, unused on disk
    int32_t anim_block_name_index;  // -> "models/m_anm.ani", from the file start
    int32_t num_anim_blocks;
    int32_t anim_block_index;
};

/// mstudiomodelgroup_t (8 bytes). Offsets are relative to this struct.
struct SourceModelGroup {
    int32_t label_index;
    int32_t name_index;   // e.g. "models/player/cs_fix/anims.mdl"
};

/// mstudioanimblock_t (8 bytes). A byte range inside the companion .ani file.
/// Index 0 is reserved to mean "not blocked", so its contents are never read.
struct SourceAnimBlock {
    int32_t data_start;
    int32_t data_end;
};

/// mstudioanimsections_t (8 bytes). Long animations are further chopped into sections,
/// each of which may live in a different block, so frame 0 must be looked up through
/// section 0 rather than through the descriptor directly.
struct SourceAnimSection {
    int32_t anim_block;
    int32_t anim_index;
};

/// Source 1 Bone structure (216 bytes)
struct SourceStudioBone {
    int32_t name_index;     // Offset to bone name string
    int32_t parent;         // Parent bone index (-1 for root)
    int32_t bone_controller[6];
    float pos[3];           // Position
    float rot[4];           // Quaternion rotation
    float radrot[3];
    float pos_scale[3];
    float rot_scale[3];
    float pose_to_bone[3][4]; // 3x4 matrix
    float q_alignment[4];
    int32_t flags;
    int32_t proc_type;
    int32_t proc_index;
    int32_t physics_bone;
    int32_t surface_prop_idx;
    int32_t contents;
    int32_t unused[8];
};

/// mstudiobodyparts_t (16 bytes)
struct SourceBodyPart {
    int32_t name_index;
    int32_t num_models;
    int32_t base;
    int32_t model_index;  // relative to the start of THIS struct
};

/// mstudiomodel_t (148 bytes)
struct SourceModel {
    char name[64];
    int32_t type;
    float bounding_radius;
    int32_t num_meshes;
    int32_t mesh_index;      // relative to the start of THIS struct
    int32_t num_vertices;
    int32_t vertex_index;    // BYTE offset into the .vvd vertex array
    int32_t tangents_index;
    int32_t num_attachments;
    int32_t attachment_index;
    int32_t num_eyeballs;
    int32_t eyeball_index;
    int32_t vertex_data[2];  // runtime pointers, unused on disk
    int32_t unused[8];
};

/// mstudiomesh_t (116 bytes)
struct SourceMesh {
    int32_t material;        // index into the texture table
    int32_t model_index;
    int32_t num_vertices;
    int32_t vertex_offset;   // first vertex of this mesh, relative to its model
    int32_t num_flexes;
    int32_t flex_index;
    int32_t material_type;
    int32_t material_param;
    int32_t mesh_id;
    float center[3];
    int32_t vertex_data_ptr;      // runtime pointer, unused on disk
    int32_t num_lod_vertexes[8];
    int32_t unused[8];
};

/// mstudiotexture_t (64 bytes). Source stores only the material NAME here; the pixels
/// live in a separate .vtf resolved through the VFS.
struct SourceTexture {
    int32_t name_index;      // relative to the start of THIS struct
    int32_t flags;
    int32_t used;
    int32_t unused1;
    int32_t material_ptr;
    int32_t client_material_ptr;
    int32_t unused[10];
};
#pragma pack(pop)

static_assert(sizeof(SourceStudioHeader) >= 180, "SourceStudioHeader base size mismatch");
static_assert(sizeof(SourceStudioBone) == 216, "SourceStudioBone size mismatch");
static_assert(sizeof(SourceBodyPart) == 16, "SourceBodyPart size mismatch");
static_assert(sizeof(SourceModel) == 148, "SourceModel size mismatch");
static_assert(sizeof(SourceMesh) == 116, "SourceMesh size mismatch");
static_assert(sizeof(SourceTexture) == 64, "SourceTexture size mismatch");

// The header is truncated on purpose, so sizeof() pins nothing. Pin the offsets the
// parser actually indexes by. These match studiohdr_t from the Source SDK.
static_assert(offsetof(SourceStudioHeader, num_bones) == 156, "num_bones offset drift");
static_assert(offsetof(SourceStudioHeader, bone_index) == 160, "bone_index offset drift");
static_assert(offsetof(SourceStudioHeader, num_textures) == 204, "num_textures offset drift");
static_assert(offsetof(SourceStudioHeader, texture_index) == 208, "texture_index offset drift");
static_assert(offsetof(SourceStudioHeader, num_bodyparts) == 232, "num_bodyparts offset drift");
static_assert(offsetof(SourceStudioHeader, bodypart_index) == 236, "bodypart_index offset drift");
static_assert(offsetof(SourceStudioHeader, num_local_seq) == 188, "num_local_seq offset drift");
static_assert(offsetof(SourceStudioHeader, local_seq_index) == 192, "local_seq_index offset drift");
static_assert(offsetof(SourceStudioHeader, num_include_models) == 336, "num_include_models offset drift");
static_assert(offsetof(SourceStudioHeader, include_model_index) == 340, "include_model_index offset drift");
static_assert(offsetof(SourceStudioHeader, anim_block_name_index) == 348, "anim_block_name_index offset drift");
static_assert(offsetof(SourceStudioHeader, num_anim_blocks) == 352, "num_anim_blocks offset drift");
static_assert(offsetof(SourceStudioHeader, anim_block_index) == 356, "anim_block_index offset drift");
static_assert(sizeof(SourceModelGroup) == 8, "SourceModelGroup size mismatch");
static_assert(sizeof(SourceAnimBlock) == 8, "SourceAnimBlock size mismatch");
static_assert(sizeof(SourceAnimSection) == 8, "SourceAnimSection size mismatch");

} // namespace quebratsk::parsers::source1
