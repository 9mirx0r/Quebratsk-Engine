#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace quebratsk::vfs {

enum class EngineNamespace {
    GoldSrc,    // vfs://cs16/ or vfs://goldsrc/
    Source1,    // vfs://gmod/ or vfs://source1/
    RV,         // vfs://dayz/ or vfs://rv/
    BSP,        // vfs://bsp/
    Custom,     // Custom mount scheme
};

/// VFSUri represents a parsed URI in the format: vfs://mount_prefix/path/to/asset.ext
struct VFSUri {
    std::string mount_prefix;  // e.g. "cs16", "gmod", "dayz"
    std::string path;          // e.g. "models/weapons/w_awp.mdl"
    EngineNamespace engine = EngineNamespace::Custom;

    /// Parse URI string like "vfs://gmod/models/weapons/w_awp.mdl"
    [[nodiscard]] static std::optional<VFSUri> parse(std::string_view uri_str);

    /// Convert back to URI string
    [[nodiscard]] std::string to_string() const;
};

} // namespace quebratsk::vfs
