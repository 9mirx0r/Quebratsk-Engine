#include "lazy_memory_mapper.h"
#include <stdexcept>
#include <iostream>

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

    // Windows requires the offset to be a multiple of the system allocation granularity.
    // For this implementation, we assume offset is correctly aligned or we map from 0 and adjust the pointer.
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    DWORD granularity = sys_info.dwAllocationGranularity;
    
    DWORD aligned_offset = (offset / granularity) * granularity;
    DWORD offset_diff = offset - aligned_offset;
    SIZE_T mapped_size = length + offset_diff;

    DWORD offset_high = (DWORD)((aligned_offset >> 31) >> 1); // 64-bit shift
    DWORD offset_low = (DWORD)(aligned_offset & 0xFFFFFFFF);

    void* raw_view = MapViewOfFile(_mapping_handle, FILE_MAP_READ, offset_high, offset_low, mapped_size);
    if (!raw_view) {
        unmap();
        return false;
    }

    _mapped_view = static_cast<char*>(raw_view) + offset_diff;
    _current_offset = offset;
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
    
    _mapped_view = static_cast<char*>(raw_view) + offset_diff;
    _current_offset = offset;
    _current_length = length;
    return true;
#endif
}

void LazyMemoryMapper::unmap() {
#if defined(_WIN32)
    if (_mapped_view) {
        // We need to unmap the raw pointer, which might be offset backwards to the allocation granularity
        // but for safety in this stub we just call UnmapViewOfFile (Windows tracks the base).
        // Ideally we keep the raw_view pointer stored.
        // Assuming Windows can resolve the base address:
        MEMORY_BASIC_INFORMATION mbi;
        VirtualQuery(_mapped_view, &mbi, sizeof(mbi));
        UnmapViewOfFile(mbi.AllocationBase);
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
    if (_mapped_view && _mapped_view != MAP_FAILED) {
        // Calculate the base pointer
        long page_size = sysconf(_SC_PAGE_SIZE);
        off_t aligned_offset = (_current_offset / page_size) * page_size;
        size_t offset_diff = _current_offset - aligned_offset;
        void* raw_view = static_cast<char*>(_mapped_view) - offset_diff;
        
        munmap(raw_view, _mapped_length);
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
