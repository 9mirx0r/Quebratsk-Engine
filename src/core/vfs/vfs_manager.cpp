#include "vfs_manager.h"
#include "../../parsers/goldsrc/structs/wad3_structs.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cctype>

namespace quebratsk::vfs {

using namespace godot;

void VFSManager::_bind_methods() {
    ClassDB::bind_method(D_METHOD("mount_container", "vfs_prefix", "real_path"), &VFSManager::mount_container);
    ClassDB::bind_method(D_METHOD("unmount", "vfs_prefix"), &VFSManager::unmount);
    ClassDB::bind_method(D_METHOD("file_exists", "vfs_uri"), &VFSManager::file_exists);
    ClassDB::bind_method(D_METHOD("read_file", "vfs_uri"), &VFSManager::read_file);
    ClassDB::bind_method(D_METHOD("get_file_size", "vfs_uri"), &VFSManager::get_file_size);
}

static std::string to_lower(std::string_view str) {
    std::string result(str);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
    return result;
}

bool VFSManager::mount_container(const String& vfs_prefix, const String& real_path) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string prefix_std = to_lower(vfs_prefix.utf8().get_data());
    std::string path_std = real_path.utf8().get_data();

    // Strip "vfs://" if user included it in prefix
    if (prefix_std.starts_with("vfs://")) {
        prefix_std = prefix_std.substr(6);
    }
    if (prefix_std.ends_with("/")) {
        prefix_std.pop_back();
    }

    auto mmap_res = MemoryMappedFile::open(path_std);
    if (!mmap_res.has_value()) {
        UtilityFunctions::printerr("[QuebratskVFS] Failed to memory map container: ", real_path);
        return false;
    }

    MountedContainer container;
    container.real_path = path_std;
    container.mount_prefix = prefix_std;
    container.mapped_file = std::move(mmap_res.value());

    size_t container_idx = m_containers.size();

    // Auto-detect format and index
    auto bytes = container.mapped_file.bytes();
    if (parsers::goldsrc::validate_wad3_header(bytes)) {
        container.engine = EngineNamespace::GoldSrc;
        m_containers.push_back(std::move(container));
        index_wad3(container_idx);
        UtilityFunctions::print("[QuebratskVFS] Mounted GoldSrc WAD3 container at vfs://", String(prefix_std.c_str()), "/");
        return true;
    }

    UtilityFunctions::printerr("[QuebratskVFS] Unknown container format for: ", real_path);
    return false;
}

void VFSManager::unmount(const String& vfs_prefix) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string prefix_std = to_lower(vfs_prefix.utf8().get_data());

    // Remove from index entries matching prefix
    for (auto it = m_index.begin(); it != m_index.end(); ) {
        if (it->second.virtual_path.starts_with("vfs://" + prefix_std + "/")) {
            it = m_index.erase(it);
        } else {
            ++it;
        }
    }
}

void VFSManager::index_wad3(size_t container_idx) {
    const auto& container = m_containers[container_idx];
    auto bytes = container.mapped_file.bytes();

    auto* header = reinterpret_cast<const parsers::goldsrc::WAD3Header*>(bytes.data());
    if (header->dir_offset <= 0 || static_cast<size_t>(header->dir_offset) >= bytes.size()) {
        return;
    }

    size_t num_lumps = static_cast<size_t>(header->num_lumps);
    size_t dir_ofs = static_cast<size_t>(header->dir_offset);

    if (dir_ofs + num_lumps * sizeof(parsers::goldsrc::WAD3Lump) > bytes.size()) {
        return;
    }

    auto* lumps = reinterpret_cast<const parsers::goldsrc::WAD3Lump*>(bytes.data() + dir_ofs);

    for (size_t i = 0; i < num_lumps; ++i) {
        const auto& lump = lumps[i];
        std::string lump_name(lump.name, strnlen(lump.name, 16));
        std::string vfs_uri = "vfs://" + container.mount_prefix + "/" + to_lower(lump_name);

        VFSEntry entry;
        entry.virtual_path = vfs_uri;
        entry.container_index = container_idx;
        entry.offset = static_cast<size_t>(lump.file_pos);
        entry.disk_size = static_cast<size_t>(lump.disk_size);
        entry.uncompressed_size = static_cast<size_t>(lump.uncompressed_size);
        entry.compression = CompressionType::None;

        m_index[vfs_uri] = entry;
    }
}

bool VFSManager::file_exists(const String& vfs_uri) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string uri_std = to_lower(vfs_uri.utf8().get_data());
    return m_index.contains(uri_std);
}

int64_t VFSManager::get_file_size(const String& vfs_uri) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string uri_std = to_lower(vfs_uri.utf8().get_data());
    auto it = m_index.find(uri_std);
    if (it != m_index.end()) {
        return static_cast<int64_t>(it->second.uncompressed_size);
    }
    return -1;
}

std::optional<std::span<const std::byte>> VFSManager::get_raw_span(const std::string& vfs_uri_str) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string uri_std = to_lower(vfs_uri_str);
    auto it = m_index.find(uri_std);
    if (it == m_index.end()) return std::nullopt;

    const auto& entry = it->second;
    if (entry.compression != CompressionType::None) {
        return std::nullopt; // Compressed entries require decompress vector, not raw span
    }

    const auto& container = m_containers[entry.container_index];
    auto mapped_bytes = container.mapped_file.bytes();

    if (entry.offset + entry.disk_size > mapped_bytes.size()) {
        return std::nullopt;
    }

    return mapped_bytes.subspan(entry.offset, entry.disk_size);
}

PackedByteArray VFSManager::read_file(const String& vfs_uri) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    PackedByteArray result;

    std::string uri_std = to_lower(vfs_uri.utf8().get_data());
    auto it = m_index.find(uri_std);
    if (it == m_index.end()) {
        UtilityFunctions::printerr("[QuebratskVFS] File not found: ", vfs_uri);
        return result;
    }

    const auto& entry = it->second;
    const auto& container = m_containers[entry.container_index];
    auto mapped_bytes = container.mapped_file.bytes();

    if (entry.offset + entry.disk_size > mapped_bytes.size()) {
        UtilityFunctions::printerr("[QuebratskVFS] Out of bounds entry for: ", vfs_uri);
        return result;
    }

    std::span<const std::byte> entry_bytes = mapped_bytes.subspan(entry.offset, entry.disk_size);

    if (entry.compression == CompressionType::LZSS_Bohemia) {
        auto decomp_res = LZSSDecompressor::decompress(entry_bytes, entry.uncompressed_size);
        if (decomp_res.has_value()) {
            result.resize(static_cast<int64_t>(decomp_res->size()));
            std::memcpy(result.ptrw(), decomp_res->data(), decomp_res->size());
        }
    } else {
        // Zero-copy copy into PackedByteArray output
        result.resize(static_cast<int64_t>(entry.disk_size));
        std::memcpy(result.ptrw(), entry_bytes.data(), entry_bytes.size());
    }

    return result;
}

} // namespace quebratsk::vfs
