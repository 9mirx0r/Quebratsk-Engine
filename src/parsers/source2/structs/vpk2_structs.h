#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>

namespace quebratsk::parsers::source2 {

inline constexpr uint32_t kVpkMagic = 0x55AA1234;

/// An entry whose archive index is this lives inside the _dir.vpk itself rather than
/// in one of the numbered side archives.
inline constexpr uint16_t kVpkArchiveInline = 0x7FFF;

/// Every directory entry ends with this, which is a cheap way to catch a desynchronised
/// tree walk before it starts inventing files.
inline constexpr uint16_t kVpkEntryTerminator = 0xFFFF;

#pragma pack(push, 1)
/// VPK Version 1 header (12 bytes). Version 2 extends it.
struct VPK1Header {
    uint32_t magic;            // 0x55AA1234
    uint32_t version;          // 1
    uint32_t tree_size;        // size of the directory tree in bytes
};

/// VPK Version 2 header (28 bytes)
struct VPK2Header {
    uint32_t magic;            // 0x55AA1234
    uint32_t version;          // 2
    uint32_t tree_size;        // size of the directory tree in bytes
    uint32_t file_data_section_size;
    uint32_t archive_md5_section_size;
    uint32_t other_md5_section_size;
    uint32_t signature_section_size;
};

/// VPKDirectoryEntry (18 bytes), following each file name in the tree.
struct VPK2DirectoryEntry {
    uint32_t crc;
    uint16_t preload_bytes;    // inline bytes stored right after this struct
    uint16_t archive_index;    // 0x7FFF = this file, else <base>_%03d.vpk
    uint32_t entry_offset;     // for side archives, absolute; inline, past the tree
    uint32_t entry_length;
    uint16_t terminator;       // 0xFFFF
};
#pragma pack(pop)

static_assert(sizeof(VPK1Header) == 12, "VPK1Header size mismatch");
static_assert(sizeof(VPK2Header) == 28, "VPK2Header size mismatch");
static_assert(sizeof(VPK2DirectoryEntry) == 18, "VPK2DirectoryEntry size mismatch");

[[nodiscard]] inline bool validate_vpk_header(std::span<const std::byte> data) {
    if (data.size() < sizeof(VPK1Header)) return false;
    uint32_t magic = 0;
    std::memcpy(&magic, data.data(), sizeof(magic));
    return magic == kVpkMagic;
}

} // namespace quebratsk::parsers::source2
