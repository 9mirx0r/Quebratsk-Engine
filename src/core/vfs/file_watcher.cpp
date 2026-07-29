#include "file_watcher.h"
#include <filesystem>

namespace quebratsk::vfs {

void FileWatcher::add_watch(const std::string& archive_path) {
    if (std::filesystem::exists(archive_path)) {
        auto ftime = std::filesystem::last_write_time(archive_path);
        auto s_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        m_last_write_times[archive_path] = s_time;
    }
}

bool FileWatcher::check_modifications(VFSManager* vfs) {
    bool modified = false;
    for (auto& [path, last_time] : m_last_write_times) {
        if (std::filesystem::exists(path)) {
            auto current_ftime = std::filesystem::last_write_time(path);
            auto current_stime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                current_ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
            );

            if (current_stime > last_time) {
                last_time = current_stime;
                modified = true;
            }
        }
    }
    return modified;
}

} // namespace quebratsk::vfs
