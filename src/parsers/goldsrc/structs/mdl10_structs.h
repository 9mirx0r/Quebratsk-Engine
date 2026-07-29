#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>

namespace quebratsk::parsers::goldsrc {

inline constexpr std::array<char, 4> kMdl10Magic = {'I', 'D', 'S', 'T'};
inline constexpr int32_t kMdl10Version = 10;

#pragma pack(push, 1)
/// Header of a GoldSrc StudioMDL v10 file (244 bytes)
struct StudioHeader {
    char magic[4];          // "IDST"
    int32_t version;        // 10
    char name[64];
    int32_t length;

    float eyeposition[3];
    float min[3], max[3];
    float bbmin[3], bbmax[3];
    int32_t flags;

    int32_t num_bones;
    int32_t bone_index;

    int32_t num_bone_controllers;
    int32_t bone_controller_index;

    int32_t num_hitboxes;
    int32_t hitbox_index;

    int32_t num_seq;
    int32_t seq_index;

    int32_t num_seq_groups;
    int32_t seq_group_index;

    int32_t num_textures;
    int32_t texture_index;
    int32_t texture_def_index;

    int32_t num_skin_ref;
    int32_t num_skin_families;
    int32_t skin_index;

    int32_t num_bodyparts;
    int32_t bodypart_index;

    int32_t num_attachments;
    int32_t attachment_index;
};

/// Bone descriptor (112 bytes)
struct StudioBone {
    char name[32];
    int32_t parent;         // Parent bone index (-1 for root)
    int32_t flags;
    int32_t bone_controller[6];
    float value[6];         // Default pos x,y,z and rot x,y,z
    float scale[6];
};

/// BodyPart descriptor (76 bytes)
struct StudioBodyPart {
    char name[64];
    int32_t num_models;
    int32_t base;
    int32_t model_index;
};

/// Model descriptor (112 bytes)
struct StudioModel {
    char name[64];
    int32_t type;
    float bounding_radius;
    int32_t num_mesh;
    int32_t mesh_index;
    int32_t num_verts;
    int32_t vert_info_index;
    int32_t vert_index;
    int32_t num_norms;
    int32_t norm_info_index;
    int32_t norm_index;
    int32_t num_groups;
    int32_t group_index;
};
#pragma pack(pop)

static_assert(sizeof(StudioHeader) == 244, "StudioHeader size mismatch");
static_assert(sizeof(StudioBone) == 112, "StudioBone size mismatch");

} // namespace quebratsk::parsers::goldsrc
