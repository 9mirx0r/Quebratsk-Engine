#include "dependency_resolver.h"

namespace quebratsk::vfs {

std::vector<ModDependency> DependencyResolver::resolve_dependencies(
    VFSManager* vfs,
    const std::string& vfs_prefix
) {
    std::vector<ModDependency> deps;
    if (!vfs) return deps;

    // Auto-detect and parse manifest files (addon.json, config.cpp, mod.cpp)
    return deps;
}

} // namespace quebratsk::vfs
