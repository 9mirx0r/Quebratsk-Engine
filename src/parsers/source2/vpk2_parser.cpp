#include "vpk2_parser.h"

namespace quebratsk::parsers::source2 {

std::expected<std::vector<VPK2Entry>, VPK2ParseError> VPK2Parser::parse(
    std::span<const std::byte> vpk_bytes
) {
    if (vpk_bytes.size() < sizeof(VPK2Header)) {
        return std::unexpected(VPK2ParseError::InvalidHeader);
    }

    auto* header = reinterpret_cast<const VPK2Header*>(vpk_bytes.data());
    if (header->magic != kVpkMagic || header->version != 2) {
        return std::unexpected(VPK2ParseError::InvalidHeader);
    }

    std::vector<VPK2Entry> entries;
    // Tree parsing logic for Source 2 VPK directory entries
    return entries;
}

} // namespace quebratsk::parsers::source2
