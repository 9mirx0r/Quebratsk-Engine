#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>

namespace quebratsk::parsers::rv_enfusion {

inline constexpr uint16_t kPaaMagicDXT1 = 0xFF1; // DXT1 compressed
inline constexpr uint16_t kPaaMagicDXT5 = 0xFF5; // DXT5 compressed
inline constexpr uint16_t kPaaMagicRGBA = 0x4444; // ARGB4444

#pragma pack(push, 1)
/// Bohemia PAA Texture Header (6+ bytes)
struct PAAHeader {
    uint16_t magic;         // 0xFF1, 0xFF5, 0x4444
    uint32_t tag;           // Tag identifier
};
#pragma pack(pop)

static_assert(sizeof(PAAHeader) == 6, "PAAHeader size mismatch");

} // namespace quebratsk::parsers::rv_enfusion
