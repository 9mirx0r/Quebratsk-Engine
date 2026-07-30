#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <span>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace quebratsk::vfs {

class LazyMemoryMapper {
public:
    LazyMemoryMapper() = default;
    ~LazyMemoryMapper();

    // Non-copyable, non-movable (owns OS handles)
    LazyMemoryMapper(const LazyMemoryMapper&) = delete;
    LazyMemoryMapper& operator=(const LazyMemoryMapper&) = delete;

    /// Maps only a specific window/view of the file to save physical RAM
    bool map_view(const std::string& filepath, size_t offset, size_t length);

    /// Unmaps the currently active view
    void unmap();

    /// Get the current mapped data span
    std::span<const std::byte> get_data() const;

private:
#if defined(_WIN32)
    HANDLE _file_handle = INVALID_HANDLE_VALUE;
    HANDLE _mapping_handle = NULL;
    void* _raw_base_view = nullptr;  // Original pointer from MapViewOfFile
    void* _mapped_view = nullptr;    // Offset pointer for user access
#else
    int _fd = -1;
    void* _raw_base_view = nullptr;  // Original pointer from mmap
    void* _mapped_view = nullptr;    // Offset pointer for user access
    size_t _mapped_length = 0;       // Total mapped length including alignment padding
#endif

    size_t _current_length = 0;      // User-visible length
};

} // namespace quebratsk::vfs
