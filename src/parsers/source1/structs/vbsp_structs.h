#pragma once

#include <cstddef>
#include <cstdint>

namespace quebratsk::parsers::source1 {

// The layout of a Source level, as the Source SDK's public bspfile.h publishes it and the
// Valve Developer Wiki's "Source BSP File Format" page describes it. Only what is read is
// declared here.

/// "VBSP" as a little-endian int32.
inline constexpr int32_t kVbspMagic = 0x50534256;
inline constexpr int32_t kVbspLumpCount = 64;

/// The lumps this reader uses, by their number in the header.
enum VbspLump : int32_t {
    kLumpEntities = 0,
    kLumpPlanes = 1,
    kLumpTexData = 2,
    kLumpVertexes = 3,
    kLumpNodes = 5,
    kLumpTexInfo = 6,
    kLumpFaces = 7,
    kLumpLighting = 8,
    kLumpLeafs = 10,
    kLumpEdges = 12,
    kLumpSurfEdges = 13,
    kLumpModels = 14,
    kLumpDispInfo = 26,
    kLumpDispVerts = 33,
    kLumpGame = 35,
    kLumpPakfile = 40,
    kLumpTexDataStringData = 43,
    kLumpTexDataStringTable = 44,
    kLumpDispTris = 48,
    kLumpLeafAmbientIndexHdr = 51,
    kLumpLeafAmbientIndex = 52,
    kLumpLightingHdr = 53,
    kLumpLeafAmbientLightingHdr = 55,
    kLumpLeafAmbientLighting = 56,
    kLumpFacesHdr = 58,
};

/// texinfo_t flags (SURF_*). The ones a reader of the picture cares about.
inline constexpr int32_t kSurfSky2D = 0x2;
inline constexpr int32_t kSurfSky = 0x4;
inline constexpr int32_t kSurfWarp = 0x8;       // water and other moving surfaces
inline constexpr int32_t kSurfTrigger = 0x40;
inline constexpr int32_t kSurfNoDraw = 0x80;
inline constexpr int32_t kSurfHint = 0x100;
inline constexpr int32_t kSurfSkip = 0x200;

/// 'sprp', the static prop game lump.
inline constexpr int32_t kGameLumpStaticProps = 0x73707270;

#pragma pack(push, 1)

/// lump_t (16 bytes).
struct VbspLumpEntry {
    int32_t offset;
    int32_t length;
    int32_t version;
    char four_cc[4];   // non-zero for a compressed lump: its size before compression
};

/// dheader_t (1036 bytes).
struct VbspHeader {
    int32_t ident;   // "VBSP"
    int32_t version;
    VbspLumpEntry lumps[kVbspLumpCount];
    int32_t map_revision;
};
static_assert(sizeof(VbspHeader) == 1036);

/// dplane_t (20 bytes).
struct VbspPlane {
    float normal[3];
    float dist;
    int32_t type;
};
static_assert(sizeof(VbspPlane) == 20);

/// dtexdata_t (32 bytes).
struct VbspTexData {
    float reflectivity[3];
    int32_t name_string_table_id;
    int32_t width;
    int32_t height;
    int32_t view_width;
    int32_t view_height;
};
static_assert(sizeof(VbspTexData) == 32);

/// texinfo_t (72 bytes). Both pairs of vectors are in the level's own units: the texture's
/// in texels per unit, the light's in luxels per unit.
struct VbspTexInfo {
    float texture_vecs[2][4];
    float lightmap_vecs[2][4];
    int32_t flags;
    int32_t texdata;
};
static_assert(sizeof(VbspTexInfo) == 72);

/// dface_t (56 bytes).
struct VbspFace {
    uint16_t plane;
    uint8_t side;
    uint8_t on_node;
    int32_t first_edge;
    int16_t num_edges;
    int16_t texinfo;
    int16_t dispinfo;              // -1 for a face that is not a displacement
    int16_t surface_fog_volume_id;
    uint8_t styles[4];
    int32_t light_offset;          // into the lighting lump, in bytes; -1 for an unlit face
    float area;
    int32_t lightmap_mins[2];      // in luxels
    int32_t lightmap_size[2];      // in luxels, minus one
    int32_t original_face;
    uint16_t num_prims;
    uint16_t first_prim;
    uint32_t smoothing_groups;
};
static_assert(sizeof(VbspFace) == 56);

/// dedge_t (4 bytes).
struct VbspEdge {
    uint16_t v[2];
};

/// dmodel_t (48 bytes).
struct VbspModel {
    float mins[3];
    float maxs[3];
    float origin[3];
    int32_t head_node;
    int32_t first_face;
    int32_t num_faces;
};
static_assert(sizeof(VbspModel) == 48);

/// ddispinfo_t (176 bytes). Only the head is read; the rest says which displacements
/// border which, for the engine's own seams.
struct VbspDispInfo {
    float start_position[3];   // the corner of the base face the grid starts at
    int32_t disp_vert_start;
    int32_t disp_tri_start;
    int32_t power;             // 2, 3 or 4: a grid of 2^power + 1 points a side
    int32_t min_tess;
    float smoothing_angle;
    int32_t contents;
    uint16_t map_face;
    uint8_t pad[2];
    int32_t lightmap_alpha_start;
    int32_t lightmap_sample_position_start;
    uint8_t neighbours[88];    // CDispNeighbor[4] (12 each), CDispCornerNeighbors[4] (10 each)
    uint32_t allowed_verts[10];
};
static_assert(sizeof(VbspDispInfo) == 176);

/// CDispVert (20 bytes): how far a grid point is pushed off the base face, and the blend
/// between a displacement's two textures there (0 to 255).
struct VbspDispVert {
    float vec[3];
    float dist;
    float alpha;
};
static_assert(sizeof(VbspDispVert) == 20);

/// dnode_t (32 bytes): a split of space by a plane. A child below zero is the leaf
/// -(child + 1).
struct VbspNode {
    int32_t plane;
    int32_t children[2];   // in front of the plane, behind it
    int16_t mins[3];
    int16_t maxs[3];
    uint16_t first_face;
    uint16_t num_faces;
    int16_t area;
    int16_t pad;
};
static_assert(sizeof(VbspNode) == 32);

/// ColorRGBExp32: a colour and a shared power of two, linear light that can pass 1.
struct VbspLightSample {
    uint8_t r, g, b;
    int8_t exponent;
};
static_assert(sizeof(VbspLightSample) == 4);

/// CompressedLightCube: the light arriving from each of six directions, in the order
/// +X, -X, +Y, -Y, +Z, -Z of the level's own axes.
struct VbspLightCube {
    VbspLightSample faces[6];
};
static_assert(sizeof(VbspLightCube) == 24);

/// dleaf_t as lump version 1 writes it (32 bytes): one of the pieces of open space the
/// nodes divide the level into.
struct VbspLeaf {
    int32_t contents;
    int16_t cluster;
    int16_t area_flags;
    int16_t mins[3];
    int16_t maxs[3];
    uint16_t first_leaf_face;
    uint16_t num_leaf_faces;
    uint16_t first_leaf_brush;
    uint16_t num_leaf_brushes;
    int16_t leaf_water_data_id;
    int16_t pad;
};
static_assert(sizeof(VbspLeaf) == 32);

/// dleaf_t as lump version 0 writes it (56 bytes): the same, with one light cube for the
/// whole leaf kept inside it.
struct VbspLeafV0 {
    int32_t contents;
    int16_t cluster;
    int16_t area_flags;
    int16_t mins[3];
    int16_t maxs[3];
    uint16_t first_leaf_face;
    uint16_t num_leaf_faces;
    uint16_t first_leaf_brush;
    uint16_t num_leaf_brushes;
    int16_t leaf_water_data_id;
    VbspLightCube ambient;
    int16_t pad;
};
static_assert(sizeof(VbspLeafV0) == 56);

/// dleafambientindex_t (4 bytes): which of the light samples are a leaf's.
struct VbspLeafAmbientIndex {
    uint16_t sample_count;
    uint16_t first_sample;
};
static_assert(sizeof(VbspLeafAmbientIndex) == 4);

/// dleafambientlighting_t (28 bytes): the light at one spot of a leaf. The spot is given
/// as fractions of the leaf's box, 0 at its mins and 255 at its maxs.
struct VbspLeafAmbientSample {
    VbspLightCube cube;
    uint8_t x, y, z;
    uint8_t pad;
};
static_assert(sizeof(VbspLeafAmbientSample) == 28);

/// dgamelump_t (16 bytes).
struct VbspGameLump {
    int32_t id;
    uint16_t flags;
    uint16_t version;
    int32_t offset;   // from the start of the file
    int32_t length;
};
static_assert(sizeof(VbspGameLump) == 16);

#pragma pack(pop)

} // namespace quebratsk::parsers::source1
