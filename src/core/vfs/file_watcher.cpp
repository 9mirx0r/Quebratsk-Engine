#include "file_watcher.h"
#include <filesystem>

namespace quebratsk::vfs {

namespace {

/// std::filesystem's throwing overloads abort the process when exceptions are
/// disabled (godot-cpp sets _HAS_EXCEPTIONS=0), and a watched file can legitimately
/// vanish between the exists() and last_write_time() calls. Always use error_code.
bool try_read_write_time(const std::string& path, std::filesystem::file_time_type& out) {
    std::error_code ec;
    out = std::filesystem::last_write_time(path, ec);
    return !ec;
}

} // namespace

void FileWatcher::add_watch(const std::string& archive_path) {
    std::filesystem::file_time_type stamp;
    if (!try_read_write_time(archive_path, stamp)) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_last_write_times[archive_path] = stamp;
}

std::vector<std::string> FileWatcher::poll_modified() {
    std::vector<std::string> modified;

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [path, last_time] : m_last_write_times) {
        std::filesystem::file_time_type current;
        if (!try_read_write_time(path, current)) continue; // deleted or unreadable

        if (current > last_time) {
            last_time = current;
            modified.push_back(path);
        }
    }

    return modified;
}

} // namespace quebratsk::vfs
