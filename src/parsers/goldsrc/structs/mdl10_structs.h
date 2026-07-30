#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>

namespace quebratsk::parsers::goldsrc {

// Triangle command stream markers. Each command is an int16 count followed by
// `abs(count)` vertex records of 4 int16 (vertex, normal, s, t).
inline constexpr int16_t kTriCommandEnd = 0; // negative = fan, positive = strip

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
    int32_t num_verts;       // unique vertex positions
    int32_t vert_info_index; // uint8 per vertex: which bone it is attached to
    int32_t vert_index;      // vec3 array, in BONE-LOCAL space
    int32_t num_norms;
    int32_t norm_info_index; // uint8 per normal: owning bone
    int32_t norm_index;      // vec3 array, bone-local
    int32_t num_groups;
    int32_t group_index;
};

/// Mesh descriptor (20 bytes). `tri_index` points at the triangle command stream.
struct StudioMesh {
    int32_t num_tris;
    int32_t tri_index;
    int32_t skin_ref;   // index into the skin table, which maps to a texture
    int32_t num_norms;
    int32_t norm_index;
};

/// Texture descriptor (80 bytes). Present only when the model embeds its textures;
/// otherwise they live in a companion "<name>T.mdl" and num_textures is 0.
struct StudioTexture {
    char name[64];
    int32_t flags;
    int32_t width;
    int32_t height;
    int32_t index;   // offset to 8-bit palettized pixels
};
#pragma pack(pop)

static_assert(sizeof(StudioHeader) >= 200, "StudioHeader size mismatch");
static_assert(sizeof(StudioBone) == 112, "StudioBone size mismatch");
static_assert(sizeof(StudioBodyPart) == 76, "StudioBodyPart size mismatch");
static_assert(sizeof(StudioModel) == 112, "StudioModel size mismatch");
static_assert(sizeof(StudioMesh) == 20, "StudioMesh size mismatch");
static_assert(sizeof(StudioTexture) == 80, "StudioTexture size mismatch");

// The header struct is deliberately truncated after attachment_index (sound and
// transition tables are unused), so sizeof() cannot pin the layout. Pin the field
// offsets the parser actually indexes by instead.
static_assert(offsetof(StudioHeader, num_bones) == 140, "num_bones offset drift");
static_assert(offsetof(StudioHeader, bone_index) == 144, "bone_index offset drift");
static_assert(offsetof(StudioHeader, num_textures) == 180, "num_textures offset drift");
static_assert(offsetof(StudioHeader, texture_index) == 184, "texture_index offset drift");
static_assert(offsetof(StudioHeader, num_skin_ref) == 192, "num_skin_ref offset drift");
static_assert(offsetof(StudioHeader, skin_index) == 200, "skin_index offset drift");
static_assert(offsetof(StudioHeader, num_bodyparts) == 204, "num_bodyparts offset drift");
static_assert(offsetof(StudioHeader, bodypart_index) == 208, "bodypart_index offset drift");

} // namespace quebratsk::parsers::goldsrc
