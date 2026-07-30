#pragma once

#include <cstddef>
#include <cstdint>

namespace quebratsk::parsers::source1 {

inline constexpr int32_t kVtxVersion = 7;

#pragma pack(push, 1)
/// VTX (.dx90.vtx) supplies the index data that the .mdl and .vvd lack.
///
/// IMPORTANT: every *_offset field below is relative to the address of the struct that
/// contains it, NOT to the start of the file. Treating them as absolute is the other
/// classic mistake with this format.

/// FileHeader_t (36 bytes)
struct VTXHeader {
    int32_t version;                          // 7
    int32_t vert_cache_size;
    uint16_t max_bones_per_strip;
    uint16_t max_bones_per_tri;
    int32_t max_bones_per_vert;
    int32_t checksum;                         // must match the .mdl and .vvd
    int32_t num_lods;
    int32_t material_replacement_list_offset;
    int32_t num_body_parts;
    int32_t body_part_offset;
};

/// BodyPartHeader_t (8 bytes)
struct VTXBodyPart {
    int32_t num_models;
    int32_t model_offset;
};

/// ModelHeader_t (8 bytes)
struct VTXModel {
    int32_t num_lods;
    int32_t lod_offset;
};

/// ModelLODHeader_t (12 bytes)
struct VTXModelLOD {
    int32_t num_meshes;
    int32_t mesh_offset;
    float switch_point;
};

/// MeshHeader_t (9 bytes)
struct VTXMesh {
    int32_t num_strip_groups;
    int32_t strip_group_header_offset;
    uint8_t flags;
};

/// StripGroupHeader_t (25 bytes)
struct VTXStripGroup {
    int32_t num_verts;
    int32_t vert_offset;
    int32_t num_indices;
    int32_t index_offset;
    int32_t num_strips;
    int32_t strip_offset;
    uint8_t flags;
};

/// StripHeader_t (27 bytes)
struct VTXStrip {
    int32_t num_indices;
    int32_t index_offset;
    int32_t num_verts;
    int32_t vert_offset;
    int16_t num_bones;
    uint8_t flags;
    int32_t num_bone_state_changes;
    int32_t bone_state_change_offset;
};

/// Vertex_t (9 bytes). `orig_mesh_vert_id` is relative to the mesh's vertex range,
/// which is where the .mdl's model/mesh vertex offsets come back into play.
struct VTXVertex {
    uint8_t bone_weight_index[3];
    uint8_t num_bones;
    uint16_t orig_mesh_vert_id;
    int8_t bone_id[3];
};
#pragma pack(pop)

/// StripHeader_t::flags
inline constexpr uint8_t kVtxStripIsTriList  = 0x01;
inline constexpr uint8_t kVtxStripIsTriStrip = 0x02;

static_assert(sizeof(VTXHeader) == 36, "VTXHeader size mismatch");
static_assert(sizeof(VTXBodyPart) == 8, "VTXBodyPart size mismatch");
static_assert(sizeof(VTXModel) == 8, "VTXModel size mismatch");
static_assert(sizeof(VTXModelLOD) == 12, "VTXModelLOD size mismatch");
static_assert(sizeof(VTXMesh) == 9, "VTXMesh size mismatch");
static_assert(sizeof(VTXStripGroup) == 25, "VTXStripGroup size mismatch");
static_assert(sizeof(VTXStrip) == 27, "VTXStrip size mismatch");
static_assert(sizeof(VTXVertex) == 9, "VTXVertex size mismatch");

} // namespace quebratsk::parsers::source1
