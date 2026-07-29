#include "vfs_uri.h"
#include <algorithm>

namespace quebratsk::vfs {

std::optional<VFSUri> VFSUri::parse(std::string_view uri_str) {
    constexpr std::string_view kScheme = "vfs://";
    if (!uri_str.starts_with(kScheme)) {
        return std::nullopt;
    }

    std::string_view remainder = uri_str.substr(kScheme.length());
    size_t slash_pos = remainder.find('/');
    if (slash_pos == std::string_view::npos || slash_pos == 0) {
        return std::nullopt;
    }

    VFSUri uri;
    uri.mount_prefix = std::string(remainder.substr(0, slash_pos));
    uri.path = std::string(remainder.substr(slash_pos + 1));

    // Normalize path separators to forward slashes
    std::replace(uri.path.begin(), uri.path.end(), '\\', '/');

    // Determine namespace from prefix if standard
    if (uri.mount_prefix == "goldsrc" || uri.mount_prefix == "cs16" || uri.mount_prefix == "hl1") {
        uri.engine = EngineNamespace::GoldSrc;
    } else if (uri.mount_prefix == "source1" || uri.mount_prefix == "gmod" || uri.mount_prefix == "hl2") {
        uri.engine = EngineNamespace::Source1;
    } else if (uri.mount_prefix == "rv" || uri.mount_prefix == "dayz" || uri.mount_prefix == "arma") {
        uri.engine = EngineNamespace::RV;
    } else if (uri.mount_prefix == "bsp") {
        uri.engine = EngineNamespace::BSP;
    } else {
        uri.engine = EngineNamespace::Custom;
    }

    return uri;
}

std::string VFSUri::to_string() const {
    return "vfs://" + mount_prefix + "/" + path;
}

} // namespace quebratsk::vfs
