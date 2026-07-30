#include "ue_pak_parser.h"

namespace quebratsk::parsers::unreal {

std::vector<UEPakEntry> UEPakParser::parse(
    std::span<const std::byte> pak_bytes
) {
    if (pak_bytes.size() < sizeof(UEPakFooter)) {
        return {};
    }

    // Pak footer reading & index table parsing stub
    return {};
}

} // namespace quebratsk::parsers::unreal
