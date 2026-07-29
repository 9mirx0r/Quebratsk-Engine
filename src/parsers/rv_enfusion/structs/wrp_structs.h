#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>

namespace quebratsk::parsers::rv_enfusion {

inline constexpr std::array<char, 4> kWrpMagicOPRW = {'O', 'P', 'R', 'W'};
inline constexpr std::array<char, 4> kWrpMagic8WVR = {'8', 'W', 'V', 'R'};

#pragma pack(push, 1)
/// Bohemia WRP Terrain Map Header (16 bytes)
struct WRPHeader {
    char magic[4];          // "OPRW" or "8WVR"
    uint32_t version;       // 18, 22, 23
    uint32_t grid_width;    // E.g. 512, 1024, 2048
    uint32_t grid_height;
};
#pragma pack(pop)

static_assert(sizeof(WRPHeader) == 16, "WRPHeader size mismatch");

} // namespace quebratsk::parsers::rv_enfusion
