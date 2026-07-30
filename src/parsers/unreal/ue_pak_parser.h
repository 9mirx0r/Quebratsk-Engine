#pragma once

#include "structs/ue_pak_structs.h"

#include <span>
#include <string>
#include <vector>

namespace quebratsk::parsers::unreal {

enum class UEPakParseError {
    InvalidHeader,
    UnsupportedVersion,
    CorruptedData,
};

struct UEPakEntry {
    std::string path;
    int64_t offset = 0;
    int64_t size = 0;
    int64_t uncompressed_size = 0;
    uint32_t compression_method = 0;
};

class UEPakParser {
public:
    /// Index an Unreal Engine 4/5 .pak archive container
    [[nodiscard]] static std::vector<UEPakEntry> parse(
        std::span<const std::byte> pak_bytes
    );
};

} // namespace quebratsk::parsers::unreal
