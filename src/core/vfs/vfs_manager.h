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

/// Indexed entry: either a slice of a mounted container, or a loose file on disk.
struct VFSEntry {
    std::string virtual_path;     // Normalized VFS URI path (lowercase)
    size_t container_index = 0;   // Index into m_containers
    size_t offset = 0;            // Byte offset in container
    size_t disk_size = 0;         // Stored size on disk
    size_t uncompressed_size = 0; // Final size
    CompressionType compression = CompressionType::None;

    /// Non-empty when this entry is a standalone file mounted via mount_directory()
    /// rather than a slice of a container. Such entries are read on demand instead of
    /// being memory-mapped, so mounting a large tree costs no OS handles.
    std::string loose_path;

    [[nodiscard]] bool is_loose() const { return !loose_path.empty(); }
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

    /// Mount a directory tree of loose files at a VFS path prefix.
    ///
    /// Extracted asset folders are the common case for modders, and archives were the
    /// only thing this VFS could read. Files are indexed by relative path and read on
    /// demand, so no memory mapping or OS handle is held per file.
    ///
    /// Returns the number of files indexed; 0 means the directory was missing or empty.
    int64_t mount_directory(const godot::String& vfs_prefix, const godot::String& real_dir);

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

    /// Get raw zero-copy span for uncompressed entries.
    /// WARNING: the span is only valid until the owning container is unmounted. Prefer
    /// read_owned() unless the caller provably outlives no mount change.
    [[nodiscard]] std::optional<std::span<const std::byte>> get_raw_span(const std::string& vfs_uri_str) const;

    /// Copy an entry's bytes into an owned buffer, transparently decompressing.
    /// Empty result means "missing or unreadable". This is the safe counterpart to
    /// get_raw_span() and the single place that knows how to handle both cases.
    [[nodiscard]] std::vector<std::byte> read_owned(const std::string& vfs_uri_str) const;

    /// First indexed URI ending with `lowercase_suffix`, or an empty string.
    /// Legacy material references are path fragments without a mount prefix
    /// ("metal/metalwall001a"), so they can only be resolved by suffix search.
    [[nodiscard]] std::string find_by_suffix(const std::string& lowercase_suffix) const;

private:
    void index_wad3(size_t container_idx);
    void index_gma(size_t container_idx);
    void index_pbo(size_t container_idx);

    /// A VPK is a directory file plus a set of numbered side archives, so indexing one
    /// mounts several real files. `dir_real_path` is needed to derive their names.
    void index_vpk(size_t container_idx, const std::string& dir_real_path);

    /// Map a container into m_containers, reusing a slot freed by unmount().
    /// Returns the index it landed at.
    size_t place_container(MountedContainer&& container);

    std::vector<MountedContainer> m_containers;
    std::unordered_map<std::string, VFSEntry> m_index;
    mutable std::mutex m_mutex;
};

} // namespace quebratsk::vfs
