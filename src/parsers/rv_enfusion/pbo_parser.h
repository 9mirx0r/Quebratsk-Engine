#pragma once

#include "structs/pbo_structs.h"
#include "../../core/vfs/decompressors/lzss_decompressor.h"

#include <expected>
#include <span>
#include <string>
#include <vector>

namespace quebratsk::parsers::rv_enfusion {

enum class PBOParseError {
    InvalidHeader,
    CorruptedData,
};

struct PBOFileEntry {
    std::string filename;
    uint32_t packing_method = 0; // 0 = Uncompressed, 0x43707273 = Cprs
    size_t original_size = 0;
    size_t data_size = 0;
    size_t data_offset = 0;
};

struct ParsedPBOArchive {
    std::vector<PBOFileEntry> files;
};

class PBOParser {
public:
    /// Parse PBO archive directory header from raw binary data
    [[nodiscard]] static std::expected<ParsedPBOArchive, PBOParseError> parse(
        std::span<const std::byte> pbo_bytes
    );
};

} // namespace quebratsk::parsers::rv_enfusion
