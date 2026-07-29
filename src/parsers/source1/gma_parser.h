#pragma once

#include "structs/gma_structs.h"

#include <expected>
#include <span>
#include <string>
#include <vector>

namespace quebratsk::parsers::source1 {

enum class GMAParseError {
    InvalidHeader,
    VersionMismatch,
    CorruptedTable,
};

struct GMAFileRecord {
    uint32_t file_index = 0;
    std::string relative_path;
    uint64_t file_size = 0;
    uint32_t crc32 = 0;
    size_t data_offset = 0;
};

struct ParsedGMAContainer {
    std::string addon_name;
    std::string addon_description;
    std::string author_name;
    uint64_t steam_id = 0;
    std::vector<GMAFileRecord> files;
};

class GMAParser {
public:
    /// Parse GMA container directory index from raw binary data
    [[nodiscard]] static std::expected<ParsedGMAContainer, GMAParseError> parse(
        std::span<const std::byte> gma_bytes
    );
};

} // namespace quebratsk::parsers::source1
