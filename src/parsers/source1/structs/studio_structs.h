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

} // namespace quebratsk::parsers::source1
