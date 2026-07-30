#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace quebratsk::parsers::source1 {

/// "IDSV" as a little-endian int32.
inline constexpr int32_t kVvdMagic = 0x56534449;
inline constexpr int32_t kVvdVersion = 4;
inline constexpr int32_t kVvdMaxLods = 8;

#pragma pack(push, 1)
/// vertexFileHeader_t (64 bytes). A Source model's vertex data lives here, not in the
/// .mdl: positions, normals, UVs and bone weights are all in this file.
struct VVDHeader {
    int32_t id;                 // "IDSV"
    int32_t version;            // 4
    int32_t checksum;           // must match the .mdl and .vtx checksums
    int32_t num_lods;
    int32_t num_lod_vertexes[8];
    int32_t num_fixups;
    int32_t fixup_table_start;
    int32_t vertex_data_start;
    int32_t tangent_data_start;
};

/// mstudioboneweight_t (16 bytes). Up to three influences per vertex.
struct VVDBoneWeight {
    float weight[3];
    int8_t bone[3];
    uint8_t num_bones;
};

/// mstudiovertex_t (48 bytes).
struct VVDVertex {
    VVDBoneWeight bone_weights;
    float position[3];
    float normal[3];
    float tex_coord[2];
};

/// vertexFileFixup_t (12 bytes).
///
/// When num_fixups > 0 the vertex array is NOT in mesh order: it must be rebuilt by
/// copying runs described by this table. Reading the array linearly instead produces a
/// scrambled mesh — this is the classic mistake with the format.
struct VVDFixup {
    int32_t lod;               // this run belongs to this LOD and every finer one
    int32_t source_vertex_id;
    int32_t num_vertexes;
};
#pragma pack(pop)

static_assert(sizeof(VVDHeader) == 64, "VVDHeader size mismatch");
static_assert(sizeof(VVDBoneWeight) == 16, "VVDBoneWeight size mismatch");
static_assert(sizeof(VVDVertex) == 48, "VVDVertex size mismatch");
static_assert(sizeof(VVDFixup) == 12, "VVDFixup size mismatch");

} // namespace quebratsk::parsers::source1
