#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>

namespace quebratsk::parsers::enfusion {

inline constexpr std::array<char, 4> kXobMagic = {'X', 'O', 'B', ' '};

#pragma pack(push, 1)
/// Bohemia Enfusion XOB Compiled Model Header (12 bytes)
struct XOBHeader {
    char magic[4];          // "XOB "
    uint32_t version;       // Version (e.g., 55, 60)
    uint32_t lod_count;     // Total LOD levels
};
#pragma pack(pop)

static_assert(sizeof(XOBHeader) == 12, "XOBHeader size mismatch");

} // namespace quebratsk::parsers::enfusion
