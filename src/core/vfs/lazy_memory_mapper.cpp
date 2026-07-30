#include "lazy_memory_mapper.h"

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace quebratsk::vfs {

LazyMemoryMapper::~LazyMemoryMapper() {
    unmap();
}

bool LazyMemoryMapper::map_view(const std::string& filepath, size_t offset, size_t length) {
    unmap();

    if (length == 0) return false;

#if defined(_WIN32)
    _file_handle = CreateFileA(filepath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (_file_handle == INVALID_HANDLE_VALUE) return false;

    _mapping_handle = CreateFileMappingA(_file_handle, NULL, PAGE_READONLY, 0, 0, NULL);
    if (_mapping_handle == NULL) {
        CloseHandle(_file_handle);
        _file_handle = INVALID_HANDLE_VALUE;
        return false;
    }

    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    DWORD granularity = sys_info.dwAllocationGranularity;

    // FIX A4: Use 64-bit arithmetic to support files > 4GB
    uint64_t aligned_offset = (static_cast<uint64_t>(offset) / granularity) * granularity;
    uint64_t offset_diff = offset - aligned_offset;
    SIZE_T mapped_size = static_cast<SIZE_T>(length + offset_diff);

    DWORD offset_high = static_cast<DWORD>(aligned_offset >> 32);
    DWORD offset_low = static_cast<DWORD>(aligned_offset & 0xFFFFFFFF);

    void* raw_view = MapViewOfFile(_mapping_handle, FILE_MAP_READ, offset_high, offset_low, mapped_size);
    if (!raw_view) {
        unmap();
        return false;
    }

    // FIX C4: Store BOTH the raw base pointer and the offset pointer
    _raw_base_view = raw_view;
    _mapped_view = static_cast<char*>(raw_view) + offset_diff;
    _current_length = length;
    return true;
#else
    _fd = open(filepath.c_str(), O_RDONLY);
    if (_fd < 0) return false;

    long page_size = sysconf(_SC_PAGE_SIZE);
    off_t aligned_offset = (offset / page_size) * page_size;
    size_t offset_diff = offset - aligned_offset;
    _mapped_length = length + offset_diff;

    void* raw_view = mmap(nullptr, _mapped_length, PROT_READ, MAP_PRIVATE, _fd, aligned_offset);
    if (raw_view == MAP_FAILED) {
        unmap();
        return false;
    }

    _raw_base_view = raw_view;
    _mapped_view = static_cast<char*>(raw_view) + offset_diff;
    _current_length = length;
    return true;
#endif
}

void LazyMemoryMapper::unmap() {
#if defined(_WIN32)
    if (_raw_base_view) {
        UnmapViewOfFile(_raw_base_view);  // Use stored base pointer directly
        _raw_base_view = nullptr;
        _mapped_view = nullptr;
    }
    if (_mapping_handle) {
        CloseHandle(_mapping_handle);
        _mapping_handle = NULL;
    }
    if (_file_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(_file_handle);
        _file_handle = INVALID_HANDLE_VALUE;
    }
#else
    if (_raw_base_view && _raw_base_view != MAP_FAILED) {
        munmap(_raw_base_view, _mapped_length);
        _raw_base_view = nullptr;
        _mapped_view = nullptr;
    }
    if (_fd >= 0) {
        close(_fd);
        _fd = -1;
    }
#endif
    _current_length = 0;
}

std::span<const std::byte> LazyMemoryMapper::get_data() const {
    if (!_mapped_view) return {};
    return {static_cast<const std::byte*>(_mapped_view), _current_length};
}

} // namespace quebratsk::vfs
