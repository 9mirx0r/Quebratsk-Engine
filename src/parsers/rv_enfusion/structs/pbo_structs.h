#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>
#include <cstring>

namespace quebratsk::parsers::rv_enfusion {

inline constexpr uint32_t kPboMagicCprs = 0x43707273; // "Cprs" FourCC (LZSS compressed)
inline constexpr uint32_t kPboMagicVers = 0x56657273; // "Vers" FourCC (Version extension)

#pragma pack(push, 1)
/// Fixed binary fields of a PBO entry header (20 bytes following null-terminated filename)
struct PBOEntryFields {
    uint32_t packing_method;   // 0 = Uncompressed, 0x43707273 = Cprs (LZSS), 0x56657273 = Vers
    uint32_t original_size;    // Uncompressed byte size
    uint32_t reserved;         // Reserved / Offset
    uint32_t timestamp;        // Unix epoch timestamp
    uint32_t data_size;        // Compressed size on disk
};
#pragma pack(pop)

static_assert(sizeof(PBOEntryFields) == 20, "PBOEntryFields size mismatch");

} // namespace quebratsk::parsers::rv_enfusion
