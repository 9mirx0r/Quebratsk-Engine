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

/// mstudioseqdesc_t (176 bytes). One named animation: "idle1", "shoot", "reload".
///
/// The fields this parser ignores are still spelled out, because a struct with holes in it
/// is a struct whose later fields are at the wrong offsets. Several carry names that differ
/// between Valve's original header and later reconstructions; where they do, the byte layout
/// is what matters and it agrees.
struct StudioSeqDesc {
    char label[32];
    float fps;
    int32_t flags;               // kSeqLooping and friends
    int32_t activity;
    int32_t act_weight;
    int32_t num_events;
    int32_t event_index;         // absolute file offset to StudioEvent[num_events]
    int32_t num_frames;
    int32_t num_pivots;
    int32_t pivot_index;
    int32_t motion_type;
    int32_t motion_bone;
    float linear_movement[3];
    int32_t auto_move_pos_index;
    int32_t auto_move_angle_index;
    float bbmin[3];
    float bbmax[3];
    int32_t num_blends;
    int32_t anim_index;          // absolute file offset to StudioAnim[num_bones], when seq_group is 0
    int32_t blend_type[2];
    float blend_start[2];
    float blend_end[2];
    int32_t blend_parent;
    int32_t seq_group;           // 0 = tracks are in this file, >0 = in a sidecar "<name>NN.mdl"
    int32_t entry_node;
    int32_t exit_node;
    int32_t node_flags;
    int32_t next_seq;
};

/// mstudioevent_t (76 bytes). What happens at a given frame besides bones moving.
///
/// Note this is NOT the Source layout, which starts with a float cycle and is 80 bytes long.
/// The two engines share a magic number and a version field and almost nothing else here.
struct StudioEvent {
    int32_t frame;      // the frame it fires on, not a fraction of the sequence
    int32_t event;      // kEventClientSound and friends
    int32_t unused;     // called "type" in older headers, never populated
    char options[64];   // a sound path relative to sound/, for sound events
};

/// mstudioanim_t (12 bytes). One per bone, six offsets relative to this struct itself.
///
/// Channels 0 to 2 are position, 3 to 5 are Euler rotation. An offset of zero means the
/// channel does not move and the bone's own rest value stands for the whole sequence, which
/// is most of them: a walk cycle animates hips and legs and leaves the fingers alone.
struct StudioAnim {
    uint16_t offset[6];
};

/// mstudioanimvalue_t (2 bytes). Either a run header or one sample, by position.
///
/// A run is {valid, total} followed by `valid` samples covering `total` frames: frames past
/// the samples repeat the last one. This is why a track cannot be indexed by frame number.
union StudioAnimValue {
    struct {
        uint8_t valid;
        uint8_t total;
    } num;
    int16_t value;
};
#pragma pack(pop)

/// mstudioseqdesc_t::flags. What makes an idle repeat instead of freezing on its last frame.
inline constexpr int32_t kSeqLooping = 0x0001;

/// mstudioevent_t::event ids that mean "play a sound". GoldSrc has no named events, so the
/// number is the only way to tell a gunshot from a muzzle flash or a shell ejecting.
inline constexpr int32_t kEventClientSound = 5004;
inline constexpr int32_t kEventScriptSound = 1004;
inline constexpr int32_t kEventScriptSoundVoice = 1008;

static_assert(sizeof(StudioHeader) >= 200, "StudioHeader size mismatch");
static_assert(sizeof(StudioBone) == 112, "StudioBone size mismatch");
static_assert(sizeof(StudioBodyPart) == 76, "StudioBodyPart size mismatch");
static_assert(sizeof(StudioModel) == 112, "StudioModel size mismatch");
static_assert(sizeof(StudioMesh) == 20, "StudioMesh size mismatch");
static_assert(sizeof(StudioTexture) == 80, "StudioTexture size mismatch");
static_assert(sizeof(StudioSeqDesc) == 176, "StudioSeqDesc size mismatch");
static_assert(sizeof(StudioEvent) == 76, "StudioEvent size mismatch");
static_assert(sizeof(StudioAnim) == 12, "StudioAnim size mismatch");
static_assert(sizeof(StudioAnimValue) == 2, "StudioAnimValue size mismatch");
static_assert(offsetof(StudioSeqDesc, anim_index) == 124, "anim_index offset drift");
static_assert(offsetof(StudioSeqDesc, seq_group) == 156, "seq_group offset drift");

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
