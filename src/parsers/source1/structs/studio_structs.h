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
#pragma pack(pop)

static_assert(sizeof(SourceStudioHeader) >= 180, "SourceStudioHeader base size mismatch");
static_assert(sizeof(SourceStudioBone) == 216, "SourceStudioBone size mismatch");

} // namespace quebratsk::parsers::source1
