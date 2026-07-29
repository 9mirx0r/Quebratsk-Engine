#include "memory_mapped_file.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace quebratsk::vfs {

MemoryMappedFile::~MemoryMappedFile() {
    close();
}

MemoryMappedFile::MemoryMappedFile(MemoryMappedFile&& other) noexcept
    : m_data(other.m_data), m_size(other.m_size) {
    other.m_data = nullptr;
    other.m_size = 0;
#ifdef _WIN32
    m_file_handle = other.m_file_handle;
    m_mapping_handle = other.m_mapping_handle;
    other.m_file_handle = nullptr;
    other.m_mapping_handle = nullptr;
#else
    m_fd = other.m_fd;
    other.m_fd = -1;
#endif
}

MemoryMappedFile& MemoryMappedFile::operator=(MemoryMappedFile&& other) noexcept {
    if (this != &other) {
        close();
        m_data = other.m_data;
        m_size = other.m_size;
        other.m_data = nullptr;
        other.m_size = 0;
#ifdef _WIN32
        m_file_handle = other.m_file_handle;
        m_mapping_handle = other.m_mapping_handle;
        other.m_file_handle = nullptr;
        other.m_mapping_handle = nullptr;
#else
        m_fd = other.m_fd;
        other.m_fd = -1;
#endif
    }
    return *this;
}

std::expected<MemoryMappedFile, MMapError> MemoryMappedFile::open(const std::string& filepath) {
    MemoryMappedFile mapped;

#ifdef _WIN32
    HANDLE file = CreateFileA(
        filepath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (file == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            return std::unexpected(MMapError::FileNotFound);
        }
        return std::unexpected(MMapError::AccessDenied);
    }

    LARGE_INTEGER size_li;
    if (!GetFileSizeEx(file, &size_li) || size_li.QuadPart == 0) {
        CloseHandle(file);
        return std::unexpected(MMapError::EmptyFile);
    }

    HANDLE mapping = CreateFileMappingA(
        file,
        NULL,
        PAGE_READONLY,
        0,
        0,
        NULL
    );

    if (!mapping) {
        CloseHandle(file);
        return std::unexpected(MMapError::MappingFailed);
    }

    void* data = MapViewOfFile(
        mapping,
        FILE_MAP_READ,
        0,
        0,
        0
    );

    if (!data) {
        CloseHandle(mapping);
        CloseHandle(file);
        return std::unexpected(MMapError::MappingFailed);
    }

    mapped.m_data = data;
    mapped.m_size = static_cast<size_t>(size_li.QuadPart);
    mapped.m_file_handle = file;
    mapped.m_mapping_handle = mapping;

#else
    int fd = ::open(filepath.c_str(), O_RDONLY);
    if (fd == -1) {
        return std::unexpected(MMapError::FileNotFound);
    }

    struct stat st;
    if (fstat(fd, &st) == -1 || st.st_size == 0) {
        ::close(fd);
        return std::unexpected(MMapError::EmptyFile);
    }

    void* data = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        ::close(fd);
        return std::unexpected(MMapError::MappingFailed);
    }

    mapped.m_data = data;
    mapped.m_size = static_cast<size_t>(st.st_size);
    mapped.m_fd = fd;
#endif

    return mapped;
}

void MemoryMappedFile::close() noexcept {
    if (!m_data) return;

#ifdef _WIN32
    UnmapViewOfFile(m_data);
    if (m_mapping_handle) CloseHandle(m_mapping_handle);
    if (m_file_handle) CloseHandle(m_file_handle);
    m_mapping_handle = nullptr;
    m_file_handle = nullptr;
#else
    munmap(m_data, m_size);
    if (m_fd != -1) ::close(m_fd);
    m_fd = -1;
#endif

    m_data = nullptr;
    m_size = 0;
}

} // namespace quebratsk::vfs
