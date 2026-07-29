#include "bundle_parser.h"
#include <cstring>

namespace quebratsk::parsers::unity {

std::expected<std::vector<BundleNode>, BundleParseError> UnityBundleParser::parse(
    std::span<const std::byte> bundle_bytes
) {
    if (bundle_bytes.size() < sizeof(UnityFSHeader)) {
        return std::unexpected(BundleParseError::InvalidHeader);
    }

    auto* header = reinterpret_cast<const UnityFSHeader*>(bundle_bytes.data());
    bool is_unityfs = (std::memcmp(header->magic, kUnityFSMagic.data(), 7) == 0);
    bool is_raw = (std::memcmp(header->magic, kUnityRawMagic.data(), 7) == 0);

    if (!is_unityfs && !is_raw) {
        return std::unexpected(BundleParseError::InvalidHeader);
    }

    std::vector<BundleNode> nodes;
    return nodes;
}

} // namespace quebratsk::parsers::unity
