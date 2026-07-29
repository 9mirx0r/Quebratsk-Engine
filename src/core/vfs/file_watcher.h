#pragma once

#include "vfs_manager.h"

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace quebratsk::vfs {

class FileWatcher {
public:
    FileWatcher() = default;

    /// Register archive path for hot-reloading change detection
    void add_watch(const std::string& archive_path);

    /// Check if any monitored archive was modified on disk
    bool check_modifications(VFSManager* vfs);

private:
    std::unordered_map<std::string, std::chrono::system_clock::time_point> m_last_write_times;
};

} // namespace quebratsk::vfs
