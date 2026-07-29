#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <expected>

namespace quebratsk::vfs {

enum class MMapError {
    FileNotFound,
    AccessDenied,
    MappingFailed,
    EmptyFile,
};

/// Cross-platform memory-mapped file wrapper (RAII)
/// Uses CreateFileMapping/MapViewOfFile on Windows and mmap on POSIX.
class MemoryMappedFile {
public:
    MemoryMappedFile() = default;
    ~MemoryMappedFile();

    MemoryMappedFile(const MemoryMappedFile&) = delete;
    MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;

    MemoryMappedFile(MemoryMappedFile&& other) noexcept;
    MemoryMappedFile& operator=(MemoryMappedFile&& other) noexcept;

    /// Open and map a file in read-only mode
    [[nodiscard]] static std::expected<MemoryMappedFile, MMapError> open(const std::string& filepath);

    /// Get read-only view of mapped bytes
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return {reinterpret_cast<const std::byte*>(m_data), m_size};
    }

    [[nodiscard]] const void* data() const noexcept { return m_data; }
    [[nodiscard]] size_t size() const noexcept { return m_size; }
    [[nodiscard]] bool is_valid() const noexcept { return m_data != nullptr && m_size > 0; }

    void close() noexcept;

private:
    void* m_data = nullptr;
    size_t m_size = 0;

#ifdef _WIN32
    void* m_file_handle = nullptr;     // HANDLE
    void* m_mapping_handle = nullptr;  // HANDLE
#else
    int m_fd = -1;
#endif
};

} // namespace quebratsk::vfs
