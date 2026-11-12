#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>

namespace quebratsk::parsers::goldsrc {

inline constexpr std::array<char, 4> kSprMagic = {'I', 'D', 'S', 'P'};
inline constexpr int32_t kSprVersion = 2;

#pragma pack(push, 1)
/// GoldSrc Sprite Header (40 bytes). The palette count (uint16, always 256)
/// follows immediately at offset 40.
struct SpriteHeader {
    char magic[4];          // "IDSP"
    int32_t version;        // 2
    int32_t type;           // Orientation mode (0=Parallel Upright, 1=Facing Upright, 2=Parallel, 3=Oriented, 4=Parallel Oriented)
    int32_t tex_format;     // Render blend mode (0=normal, 1=additive, 2=indexalpha, 3=alphatest)
    float bounding_radius;  // hypot(max_width, max_height) / 2
    int32_t max_width;
    int32_t max_height;
    int32_t num_frames;
    float beam_length;      // zero in every retail sprite
    int32_t sync_type;      // 0=sync, 1=random
};

/// Frame header — dspriteframe_t (16 bytes)
/// NOTE: the frame-group selector is a separate int32 that precedes the frame
/// (0 = single frame, 1 = group of `int32 count` frames); it is not a member of
/// dspriteframe_t. Declaring it here made sizeof() 20 instead of 16, so every
/// frame was read at the wrong offset and the cursor drifted 4 bytes per frame.
/// The retail corpus only ever uses type 0, but a reader must skip the int32.
struct SpriteFrameHeader {
    int32_t origin_x;
    int32_t origin_y;
    int32_t width;
    int32_t height;
};
#pragma pack(pop)

static_assert(sizeof(SpriteHeader) == 40, "SpriteHeader size mismatch");
static_assert(sizeof(SpriteFrameHeader) == 16, "SpriteFrameHeader size mismatch");
// The order inside the header is not the Quake dsprite_t order: tex_format sits
// at 12, before the radius. Pinned because reading width/height one field early
// shifts every frame.
static_assert(offsetof(SpriteHeader, tex_format) == 12, "tex_format offset drift");
static_assert(offsetof(SpriteHeader, bounding_radius) == 16, "bounding_radius offset drift");
static_assert(offsetof(SpriteHeader, max_width) == 20, "max_width offset drift");
static_assert(offsetof(SpriteHeader, max_height) == 24, "max_height offset drift");
static_assert(offsetof(SpriteHeader, num_frames) == 28, "num_frames offset drift");
static_assert(offsetof(SpriteHeader, beam_length) == 32, "beam_length offset drift");
static_assert(offsetof(SpriteHeader, sync_type) == 36, "sync_type offset drift");
static_assert(offsetof(SpriteFrameHeader, width) == 8, "frame width offset drift");

} // namespace quebratsk::parsers::goldsrc
