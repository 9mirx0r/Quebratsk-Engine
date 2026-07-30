#pragma once

#include "vfs_manager.h"

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace quebratsk::vfs {

class FileWatcher {
public:
    FileWatcher() = default;

    /// Register archive path for hot-reloading change detection
    void add_watch(const std::string& archive_path);

    /// Returns the paths whose on-disk timestamp changed since the last call.
    /// The caller decides what to do with them — remounting is not this class's job,
    /// which is why the old VFSManager* parameter (never used) was removed.
    [[nodiscard]] std::vector<std::string> poll_modified();

private:
    /// Timestamps are only ever compared against each other, so keep them in the
    /// filesystem's own clock. Converting to system_clock added a portability
    /// problem (MSVC's file clock exposes no to_sys) and bought nothing.
    std::unordered_map<std::string, std::filesystem::file_time_type> m_last_write_times;
    std::mutex m_mutex;
};

} // namespace quebratsk::vfs
