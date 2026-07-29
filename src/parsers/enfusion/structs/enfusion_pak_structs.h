#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>

namespace quebratsk::parsers::enfusion {

inline constexpr std::array<char, 4> kEnfusionPakMagic = {'E', 'P', 'A', 'K'};

#pragma pack(push, 1)
/// Bohemia Enfusion PAK Header (16 bytes)
struct EnfusionPakHeader {
    char magic[4];          // "EPAK"
    uint32_t version;       // Version (1, 2)
    uint32_t entry_count;   // Total files indexed
    uint32_t flags;
};
#pragma pack(pop)

static_assert(sizeof(EnfusionPakHeader) == 16, "EnfusionPakHeader size mismatch");

} // namespace quebratsk::parsers::enfusion
