#pragma once

#include "structs/bundle_structs.h"

#include <span>
#include <string>
#include <vector>

namespace quebratsk::parsers::unity {

enum class BundleParseError {
    InvalidHeader,
    UnsupportedVersion,
    CorruptedData,
};

struct BundleNode {
    std::string path;
    uint64_t offset = 0;
    uint64_t size = 0;
    uint32_t flags = 0;
};

class UnityBundleParser {
public:
    /// Index a Unity AssetBundle (.bundle) container (used in Escape from Tarkov)
    [[nodiscard]] static std::vector<BundleNode> parse(
        std::span<const std::byte> bundle_bytes
    );
};

} // namespace quebratsk::parsers::unity
