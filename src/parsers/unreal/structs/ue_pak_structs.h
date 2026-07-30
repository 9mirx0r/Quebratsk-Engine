#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>

namespace quebratsk::parsers::unreal {

inline constexpr uint32_t kUEPakMagic = 0x5A6F12E1;

#pragma pack(push, 1)
/// Unreal Engine 4/5 Pak Footer (228 bytes approx depending on version)
struct UEPakFooter {
    uint32_t magic;              // 0x5A6F12E1
    int32_t version;             // Version (e.g., 8, 9, 11)
    int64_t index_offset;        // Offset of index table
    int64_t index_size;          // Size of index table
    uint8_t index_sha1[20];      // SHA-1 hash of index table
};
#pragma pack(pop)

static_assert(sizeof(UEPakFooter) == 44, "UEPakFooter size mismatch");

} // namespace quebratsk::parsers::unreal
