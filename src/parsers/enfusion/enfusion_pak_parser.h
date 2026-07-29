#pragma once

#include "structs/enfusion_pak_structs.h"

#include <expected>
#include <span>
#include <string>
#include <vector>

namespace quebratsk::parsers::enfusion {

enum class EnfusionPakParseError {
    InvalidHeader,
    UnsupportedVersion,
    CorruptedData,
};

struct EnfusionPakEntry {
    std::string filename;
    uint64_t offset = 0;
    uint64_t compressed_size = 0;
    uint64_t uncompressed_size = 0;
};

class EnfusionPakParser {
public:
    /// Index an Arma Reforger / Enfusion .pak archive container
    [[nodiscard]] static std::expected<std::vector<EnfusionPakEntry>, EnfusionPakParseError> parse(
        std::span<const std::byte> pak_bytes
    );
};

} // namespace quebratsk::parsers::enfusion
