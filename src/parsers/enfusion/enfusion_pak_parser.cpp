#include "enfusion_pak_parser.h"
#include <cstring>

namespace quebratsk::parsers::enfusion {

std::expected<std::vector<EnfusionPakEntry>, EnfusionPakParseError> EnfusionPakParser::parse(
    std::span<const std::byte> pak_bytes
) {
    if (pak_bytes.size() < sizeof(EnfusionPakHeader)) {
        return std::unexpected(EnfusionPakParseError::InvalidHeader);
    }

    auto* header = reinterpret_cast<const EnfusionPakHeader*>(pak_bytes.data());
    if (std::memcmp(header->magic, kEnfusionPakMagic.data(), 4) != 0) {
        return std::unexpected(EnfusionPakParseError::InvalidHeader);
    }

    std::vector<EnfusionPakEntry> entries;
    entries.reserve(header->entry_count);
    return entries;
}

} // namespace quebratsk::parsers::enfusion
