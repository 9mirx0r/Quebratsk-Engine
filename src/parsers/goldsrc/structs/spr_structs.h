#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>

namespace quebratsk::parsers::goldsrc {

inline constexpr std::array<char, 4> kSprMagic = {'I', 'D', 'S', 'P'};
inline constexpr int32_t kSprVersion = 2;

#pragma pack(push, 1)
/// GoldSrc Sprite Header (36 bytes)
struct SpriteHeader {
    char magic[4];          // "IDSP"
    int32_t version;        // 2
    int32_t type;           // Orientation mode (0=Parallel Upright, 1=Facing Upright, 2=Parallel, 3=Oriented, 4=Parallel Oriented)
    int32_t tex_format;     // Render blend mode
    float bounding_radius;
    int32_t max_width;
    int32_t max_height;
    int32_t num_frames;
    float beam_length;
    int32_t sync_type;
};

/// Frame header — dspriteframe_t (16 bytes)
/// NOTE: the frame-group selector is a separate int32 that precedes the frame *only*
/// in grouped sprites; it is not a member of dspriteframe_t. Declaring it here made
/// sizeof() 20 instead of 16, so every frame was read at the wrong offset and the
/// cursor drifted 4 bytes per frame.
struct SpriteFrameHeader {
    int32_t origin_x;
    int32_t origin_y;
    int32_t width;
    int32_t height;
};
#pragma pack(pop)

static_assert(sizeof(SpriteHeader) == 40, "SpriteHeader size mismatch");
static_assert(sizeof(SpriteFrameHeader) == 16, "SpriteFrameHeader size mismatch");

} // namespace quebratsk::parsers::goldsrc
