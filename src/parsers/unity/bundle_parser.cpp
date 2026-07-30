#include "bundle_parser.h"

namespace quebratsk::parsers::unity {

std::vector<BundleNode> UnityBundleParser::parse(
    std::span<const std::byte> bundle_bytes
) {
    if (bundle_bytes.size() < sizeof(UnityFSHeader)) {
        return {};
    }

    // Header validation & parsing stub
    return {};
}

} // namespace quebratsk::parsers::unity
