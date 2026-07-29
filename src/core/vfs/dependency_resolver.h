#pragma once

#include "vfs_manager.h"

#include <string>
#include <vector>

namespace quebratsk::vfs {

struct ModDependency {
    std::string mod_id;
    std::string archive_path;
    bool required = true;
};

class DependencyResolver {
public:
    /// Scan addon manifest (addon.json in GMA, config.cpp in PBO) and resolve required parent archives
    [[nodiscard]] static std::vector<ModDependency> resolve_dependencies(
        VFSManager* vfs,
        const std::string& vfs_prefix
    );
};

} // namespace quebratsk::vfs
