#pragma once

#include <cstddef>
#include <cstdint>

namespace quebratsk::parsers::source1 {

/// Source 1 animation structures.
///
/// A Source model's poses live in two tables: sequences (what a game asks for by name,
/// "idle_all_01") which point at animation descriptors (the actual per-bone tracks).
/// The tracks themselves are run-length encoded and the rotations are quantised, so a
/// pose cannot be read out with a plain memcpy — see AnimDecoder.

#pragma pack(push, 1)

/// mstudioseqdesc_t (212 bytes). Offsets inside are relative to this struct.
struct SourceSeqDesc {
    int32_t base_ptr;
    int32_t name_index;          // sequence label, e.g. "idle_all_01"
    int32_t activity_name_index;
    int32_t flags;
    int32_t activity;
    int32_t act_weight;
    int32_t num_events;
    int32_t event_index;
    float bbmin[3];
    float bbmax[3];
    int32_t num_blends;
    int32_t anim_index_index;    // -> int16 array of animation indices (the blend grid)
    int32_t movement_index;
    int32_t group_size[2];
    int32_t param_index[2];
    float param_start[2];
    float param_end[2];
    int32_t param_parent;
    float fade_in_time;
    float fade_out_time;
    int32_t local_entry_node;
    int32_t local_exit_node;
    int32_t node_flags;
    float entry_phase;
    float exit_phase;
    float last_frame;
    int32_t next_seq;
    int32_t pose;
    int32_t num_ik_rules;
    int32_t num_auto_layers;
    int32_t auto_layer_index;
    int32_t weight_list_index;
    int32_t pose_key_index;
    int32_t num_ik_locks;
    int32_t ik_lock_index;
    int32_t key_value_index;
    int32_t key_value_size;
    int32_t cycle_pose_index;
    int32_t unused[7];
};

/// mstudioanimdesc_t (100 bytes). Offsets inside are relative to this struct.
struct SourceAnimDesc {
    int32_t base_ptr;
    int32_t name_index;
    float fps;
    int32_t flags;
    int32_t num_frames;
    int32_t num_movements;
    int32_t movement_index;
    int32_t unused1[6];
    int32_t anim_block;          // 0 = data is in this file, >0 = external .ani
    int32_t anim_index;          // -> the first SourceBoneAnim
    int32_t num_ik_rules;
    int32_t ik_rule_index;
    int32_t anim_block_ik_rule_index;
    int32_t num_local_hierarchy;
    int32_t local_hierarchy_index;
    int32_t section_index;
    int32_t section_frames;
    int16_t zero_frame_span;
    int16_t zero_frame_count;
    int32_t zero_frame_index;
    float zero_frame_stall_time;
};

/// mstudioanim_t (4 bytes). One per animated bone, chained by `next_offset`.
/// The per-bone payload follows immediately and its shape depends on `flags`.
struct SourceBoneAnim {
    uint8_t bone;
    uint8_t flags;
    int16_t next_offset;         // bytes to the next SourceBoneAnim, 0 = end of chain
};

/// mstudioanim_valueptr_t: three offsets (X, Y, Z) to RLE tracks, relative to itself.
struct SourceAnimValuePtr {
    int16_t offset[3];
};

/// mstudioanimvalue_t: either a run header or a sample, depending on position.
/// A run is `{valid, total}` followed by `valid` samples: `valid` frames are stored
/// explicitly and the run covers `total` frames, the remainder repeating the last
/// sample. This is why a track cannot simply be indexed by frame number.
union SourceAnimValue {
    struct {
        uint8_t valid;
        uint8_t total;
    } num;
    int16_t value;
};

/// Quaternion48: x:16, y:16, z:15, w-sign:1 packed into 6 bytes.
struct SourceQuat48 {
    uint16_t x;
    uint16_t y;
    uint16_t z_and_wneg;  // low 15 bits = z, top bit = sign of w
};

/// Quaternion64: x:21, y:21, z:21, w-sign:1 packed into 8 bytes.
struct SourceQuat64 {
    uint8_t data[8];
};

/// Vector48: three IEEE half-floats.
struct SourceVec48 {
    uint16_t x;
    uint16_t y;
    uint16_t z;
};

#pragma pack(pop)

/// mstudioanim_t::flags
inline constexpr uint8_t kAnimRawPos  = 0x01; // SourceVec48 follows
inline constexpr uint8_t kAnimRawRot  = 0x02; // SourceQuat48 follows
inline constexpr uint8_t kAnimAnimPos = 0x04; // SourceAnimValuePtr for position
inline constexpr uint8_t kAnimAnimRot = 0x08; // SourceAnimValuePtr for rotation
inline constexpr uint8_t kAnimDelta   = 0x10; // values are offsets from the rest pose
inline constexpr uint8_t kAnimRawRot2 = 0x20; // SourceQuat64 follows

/// mstudioseqdesc_t::flags. Only the one this project reads is named; STUDIO_LOOPING is
/// what tells an idle to repeat rather than play once and freeze on its last frame.
inline constexpr int32_t kSeqLooping = 0x0001;

static_assert(sizeof(SourceSeqDesc) == 212, "SourceSeqDesc size mismatch");
static_assert(sizeof(SourceAnimDesc) == 100, "SourceAnimDesc size mismatch");
static_assert(sizeof(SourceBoneAnim) == 4, "SourceBoneAnim size mismatch");
static_assert(sizeof(SourceAnimValuePtr) == 6, "SourceAnimValuePtr size mismatch");
static_assert(sizeof(SourceAnimValue) == 2, "SourceAnimValue size mismatch");
static_assert(sizeof(SourceQuat48) == 6, "SourceQuat48 size mismatch");
static_assert(sizeof(SourceQuat64) == 8, "SourceQuat64 size mismatch");
static_assert(sizeof(SourceVec48) == 6, "SourceVec48 size mismatch");

} // namespace quebratsk::parsers::source1
