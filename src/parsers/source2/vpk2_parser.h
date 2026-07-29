#pragma once

#include "structs/vpk2_structs.h"

#include <expected>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace quebratsk::parsers::source2 {

enum class VPK2ParseError {
    InvalidHeader,
    UnsupportedVersion,
    CorruptedData,
};

struct VPK2Entry {
    std::string filename;
    uint32_t offset = 0;
    uint32_t length = 0;
    uint16_t archive_index = 0x7FFF;
};

class VPK2Parser {
public:
    /// Index a Source 2 VPK v2 package directory tree
    [[nodiscard]] static std::expected<std::vector<VPK2Entry>, VPK2ParseError> parse(
        std::span<const std::byte> vpk_bytes
    );
};

} // namespace quebratsk::parsers::source2
