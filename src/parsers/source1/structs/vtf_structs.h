#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>

namespace quebratsk::parsers::source1 {

inline constexpr std::array<char, 4> kVtfMagic = {'V', 'T', 'F', '\0'};

#pragma pack(push, 1)
/// Header of a Valve Texture Format (VTF) file (80+ bytes)
struct VTFHeader {
    char magic[4];           // "VTF\0"
    uint32_t version[2];     // [0] = major (7), [1] = minor (0-5)
    uint32_t header_size;
    uint16_t width;
    uint16_t height;
    uint32_t flags;
    uint16_t frames;
    uint16_t first_frame;
    uint8_t padding0[4];
    float reflectivity[3];
    uint8_t padding1[4];
    float bumpmap_scale;
    uint32_t high_res_image_format;
    uint8_t mipmap_count;
    uint32_t low_res_image_format;
    uint8_t low_res_image_width;
    uint8_t low_res_image_height;
};
#pragma pack(pop)

static_assert(sizeof(VTFHeader) == 64, "VTFHeader base size mismatch");

} // namespace quebratsk::parsers::source1
