#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>

namespace quebratsk::parsers::goldsrc {

inline constexpr int32_t kBsp30Version = 30;

#pragma pack(push, 1)
struct LumpEntry {
    int32_t file_offset;
    int32_t file_length;
};

/// GoldSrc BSP v30 Main Header (124 bytes)
struct BSP30Header {
    int32_t version;       // 30
    LumpEntry lumps[15];   // 15 standard lumps
};

/// Lump 3: Vertices (12 bytes)
struct BSPVertex {
    float x, y, z;
};

/// Lump 1: Planes (20 bytes)
struct BSPPlane {
    float normal[3];
    float distance;
    int32_t type;
};

/// Lump 12: Edges (4 bytes)
struct BSPEdge {
    uint16_t v[2];
};

/// Lump 6: TexInfo (40 bytes)
struct BSPTexInfo {
    float vector_s[4]; // S projection vector + offset
    float vector_t[4]; // T projection vector + offset
    int32_t miptex_index;
    int32_t flags;
};

/// Lump 7: Faces (20 bytes)
struct BSPFace {
    uint16_t plane_index;
    uint16_t plane_side;
    int32_t first_edge_index;
    int16_t num_edges;
    int16_t texinfo_index;
    uint8_t styles[4];
    int32_t lightmap_offset;
};

/// Lump 9: ClipNodes for collision (8 bytes)
struct BSPClipNode {
    int32_t plane_index;
    int16_t children[2]; // Positive = node, negative = contents
};

/// Lump 2 header: the texture directory.
/// Followed immediately by `num_miptex` int32 offsets, each relative to the START of
/// lump 2. An offset of -1 means the texture is absent from this file. Each offset
/// points at a MiptexHeader (see wad3_structs.h) whose offsets[0] is 0 when the pixel
/// data lives in an external WAD rather than being embedded in the BSP.
struct BSPMipTexLumpHeader {
    int32_t num_miptex;
    // int32_t data_offsets[num_miptex] follows
};
#pragma pack(pop)

/// dmodel_t (64 bytes). Lump 14. One per brush model: the world is model 0 and every door,
/// lift and rotating fan is one of the rest.
///
/// `head_nodes` is why this struct matters. Index 0 is the visible BSP tree, and 1 to 3 are
/// the roots of the three clipnode trees, one per player size. Without reading this there is
/// no way to know where a hull begins, which is why the clipnodes were being read as an
/// unordered pile of planes.
struct BSPModel {
    float mins[3];
    float maxs[3];
    float origin[3];
    int32_t head_nodes[4];   // 0 = visible tree, 1..3 = the collision hulls
    int32_t num_vis_leafs;
    int32_t first_face;
    int32_t num_faces;
};

static_assert(sizeof(BSPModel) == 64, "BSPModel size mismatch");
static_assert(sizeof(BSP30Header) == 124, "BSP30Header size mismatch");
static_assert(sizeof(BSPVertex) == 12, "BSPVertex size mismatch");
static_assert(sizeof(BSPFace) == 20, "BSPFace size mismatch");
static_assert(sizeof(BSPClipNode) == 8, "BSPClipNode size mismatch");

/// A clipnode child below zero is a content type rather than another node, and only one of
/// them stops a player.
inline constexpr int16_t kContentsEmpty = -1;
inline constexpr int16_t kContentsSolid = -2;

/// Which clipnode tree describes a standing player. Hull 2 is for large monsters and hull 3
/// for crouching, and neither is what a level is walked through with.
inline constexpr int kHullStanding = 1;
static_assert(sizeof(BSPPlane) == 20, "BSPPlane size mismatch");
static_assert(sizeof(BSPEdge) == 4, "BSPEdge size mismatch");
static_assert(sizeof(BSPTexInfo) == 40, "BSPTexInfo size mismatch");
static_assert(sizeof(BSPMipTexLumpHeader) == 4, "BSPMipTexLumpHeader size mismatch");

} // namespace quebratsk::parsers::goldsrc
