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
    void* _mapped_view = nullptr;
#else
    int _fd = -1;
    void* _mapped_view = nullptr;
    size_t _mapped_length = 0;
#endif
    
    size_t _current_offset = 0;
    size_t _current_length = 0;
};

} // namespace quebratsk::vfs
