#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>

namespace quebratsk::parsers::source2 {

inline constexpr uint32_t kVmdlMagic = 0x564B5633; // "VKV3" (Valve KeyValues 3)

#pragma pack(push, 1)
/// Source 2 Compiled Resource Header (16 bytes)
struct Source2ResourceHeader {
    uint32_t file_size;
    uint16_t header_version;   // 12
    uint16_t version;          // Resource version
    uint32_t block_offset;
    uint32_t block_count;
};
#pragma pack(pop)

static_assert(sizeof(Source2ResourceHeader) == 16, "Source2ResourceHeader size mismatch");

} // namespace quebratsk::parsers::source2
