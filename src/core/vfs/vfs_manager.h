#pragma once

#include "memory_mapped_file.h"
#include "vfs_uri.h"
#include "decompressors/lzss_decompressor.h"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace quebratsk::vfs {

enum class CompressionType : uint32_t {
    None = 0,
    LZSS_Bohemia = 1,  // PBO "Cprs"
    Deflate = 2,
};

/// Indexed entry inside a container file
struct VFSEntry {
    std::string virtual_path;     // Normalized VFS URI path (lowercase)
    size_t container_index = 0;   // Index into m_containers
    size_t offset = 0;            // Byte offset in container
    size_t disk_size = 0;         // Stored size on disk
    size_t uncompressed_size = 0; // Final size
    CompressionType compression = CompressionType::None;
};

/// Mounted container archive
struct MountedContainer {
    EngineNamespace engine;
    std::string real_path;
    std::string mount_prefix;
    MemoryMappedFile mapped_file;
};

class VFSManager : public godot::Node {
    GDCLASS(VFSManager, godot::Node);

protected:
    static void _bind_methods();

public:
    VFSManager() = default;
    ~VFSManager() override = default;

    /// Mount a container archive (.wad, .gma, .pbo) at a VFS path prefix
    /// Example: mount_container("cs16", "c:/games/cs16/cstrike.wad")
    bool mount_container(const godot::String& vfs_prefix, const godot::String& real_path);

    /// Unmount a previously mounted container
    void unmount(const godot::String& vfs_prefix);

    /// Check if a VFS path exists
    bool file_exists(const godot::String& vfs_uri) const;

    /// List all indexed files matching prefix
    godot::PackedStringArray list_files(const godot::String& prefix = "") const;

    /// Read file content as PackedByteArray (decompresses automatically if needed)
    godot::PackedByteArray read_file(const godot::String& vfs_uri) const;

    /// Get uncompressed file size in bytes (-1 if not found)
    int64_t get_file_size(const godot::String& vfs_uri) const;

    /// Get raw zero-copy span for uncompressed entries
    [[nodiscard]] std::optional<std::span<const std::byte>> get_raw_span(const std::string& vfs_uri_str) const;

private:
    void index_wad3(size_t container_idx);
    void index_gma(size_t container_idx);
    void index_pbo(size_t container_idx);

    std::vector<MountedContainer> m_containers;
    std::unordered_map<std::string, VFSEntry> m_index;
    mutable std::mutex m_mutex;
};

} // namespace quebratsk::vfs
