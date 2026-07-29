#include "ue_pak_parser.h"

namespace quebratsk::parsers::unreal {

std::expected<std::vector<UEPakEntry>, UEPakParseError> UEPakParser::parse(
    std::span<const std::byte> pak_bytes
) {
    if (pak_bytes.size() < sizeof(UEPakFooter)) {
        return std::unexpected(UEPakParseError::InvalidHeader);
    }

    // Read footer from end of buffer
    const std::byte* footer_ptr = pak_bytes.data() + pak_bytes.size() - sizeof(UEPakFooter);
    auto* footer = reinterpret_cast<const UEPakFooter*>(footer_ptr);

    if (footer->magic != kUEPakMagic) {
        return std::unexpected(UEPakParseError::InvalidHeader);
    }

    std::vector<UEPakEntry> entries;
    return entries;
}

} // namespace quebratsk::parsers::unreal
