#pragma once

#include "structs/vpk2_structs.h"

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace quebratsk::parsers::source2 {

enum class VPK2ParseError {
    InvalidHeader,
    UnsupportedVersion,
    CorruptedTree,
};

/// One file listed by a VPK directory.
struct VPK2Entry {
    std::string path;            // "materials/models/foo.vtf", lowercase as stored
    uint16_t archive_index = kVpkArchiveInline;
    uint64_t offset = 0;         // absolute within the owning archive file
    uint64_t length = 0;
    uint16_t preload_bytes = 0;
    uint64_t preload_offset = 0; // absolute in the _dir.vpk, when preload_bytes > 0
    uint32_t crc = 0;

    [[nodiscard]] bool is_inline() const { return archive_index == kVpkArchiveInline; }
};

struct ParsedVPK2Directory {
    uint32_t version = 0;
    uint64_t tree_end = 0;          // where file data begins inside the _dir.vpk
    std::vector<VPK2Entry> entries;
    int32_t max_archive_index = -1; // highest numbered side archive referenced, -1 if none
};

class VPK2Parser {
public:
    /// Walk a `_dir.vpk` directory tree.
    ///
    /// The tree is three nested runs of NUL-terminated strings — extension, then path,
    /// then filename — each run ending with an empty string, with an 18-byte entry after
    /// every filename. A single miscount desynchronises the entire walk, so every
    /// entry's 0xFFFF terminator is verified rather than trusted.
    ///
    /// This does NOT read file data. Entries whose archive index is not
    /// kVpkArchiveInline live in sibling `<base>_%03d.vpk` files the caller must open.
    [[nodiscard]] static std::expected<ParsedVPK2Directory, VPK2ParseError> parse_directory(
        std::span<const std::byte> vpk_bytes
    );
};

} // namespace quebratsk::parsers::source2
