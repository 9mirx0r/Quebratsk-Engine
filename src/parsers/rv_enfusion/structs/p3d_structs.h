#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>

namespace quebratsk::parsers::rv_enfusion {

inline constexpr std::array<char, 4> kP3dMlodMagic = {'M', 'L', 'O', 'D'};
inline constexpr std::array<char, 4> kP3dOdolMagic = {'O', 'D', 'O', 'L'};

#pragma pack(push, 1)
/// Base header of Bohemia P3D 3D model file (12 bytes)
struct P3DHeader {
    char magic[4];          // "MLOD" or "ODOL"
    uint32_t version;       // MLOD = 257, ODOL = 40-75+
    uint32_t lod_count;     // Number of LOD levels
};
#pragma pack(pop)

static_assert(sizeof(P3DHeader) == 12, "P3DHeader size mismatch");

} // namespace quebratsk::parsers::rv_enfusion
