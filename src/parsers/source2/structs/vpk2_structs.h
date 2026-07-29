#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>

namespace quebratsk::parsers::source2 {

inline constexpr uint32_t kVpkMagic = 0x55AA1234;

#pragma pack(push, 1)
/// Source 2 VPK Version 2 Header (28 bytes)
struct VPK2Header {
    uint32_t magic;            // 0x55AA1234
    uint32_t version;          // 2
    uint32_t tree_size;        // Size of directory tree in bytes
    uint32_t file_data_section_size;
    uint32_t archive_md5_section_size;
    uint32_t other_md5_section_size;
    uint32_t signature_section_size;
};
#pragma pack(pop)

static_assert(sizeof(VPK2Header) == 28, "VPK2Header size mismatch");

} // namespace quebratsk::parsers::source2
