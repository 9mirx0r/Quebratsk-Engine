#include "vfs_manager.h"
#include "../../parsers/goldsrc/structs/wad3_structs.h"
#include "../../parsers/source1/structs/gma_structs.h"
#include "../../parsers/rv_enfusion/structs/pbo_structs.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace quebratsk::vfs {

using namespace godot;

void VFSManager::_bind_methods() {
    ClassDB::bind_method(D_METHOD("mount_container", "vfs_prefix", "real_path"), &VFSManager::mount_container);
    ClassDB::bind_method(D_METHOD("unmount", "vfs_prefix"), &VFSManager::unmount);
    ClassDB::bind_method(D_METHOD("file_exists", "vfs_uri"), &VFSManager::file_exists);
    ClassDB::bind_method(D_METHOD("list_files", "prefix"), &VFSManager::list_files, DEFVAL(""));
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
    auto bytes = container.mapped_file.bytes();

    // 1. Check GoldSrc WAD3
    if (parsers::goldsrc::validate_wad3_header(bytes)) {
        container.engine = EngineNamespace::GoldSrc;
        m_containers.push_back(std::move(container));
        index_wad3(container_idx);
        UtilityFunctions::print("[QuebratskVFS] Mounted GoldSrc WAD3: vfs://", String(prefix_std.c_str()), "/");
        return true;
    }

    // 2. Check Source Engine 1 GMA
    if (parsers::source1::validate_gma_header(bytes)) {
        container.engine = EngineNamespace::Source1;
        m_containers.push_back(std::move(container));
        index_gma(container_idx);
        UtilityFunctions::print("[QuebratskVFS] Mounted Source 1 GMA: vfs://", String(prefix_std.c_str()), "/");
        return true;
    }

    // 3. Check Real Virtuality / Enfusion PBO (Starts with null-terminated filename string)
    // Attempt PBO indexer validation
    container.engine = EngineNamespace::RV;
    m_containers.push_back(std::move(container));
    index_pbo(container_idx);
    UtilityFunctions::print("[QuebratskVFS] Mounted PBO Container: vfs://", String(prefix_std.c_str()), "/");
    return true;
}

void VFSManager::unmount(const String& vfs_prefix) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string prefix_std = to_lower(vfs_prefix.utf8().get_data());

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

void VFSManager::index_gma(size_t container_idx) {
    const auto& container = m_containers[container_idx];
    auto bytes = container.mapped_file.bytes();
    if (bytes.size() < sizeof(parsers::source1::GMAHeader)) return;

    size_t cursor = sizeof(parsers::source1::GMAHeader);

    // Read null-terminated string headers: Name, Description, Author
    auto skip_str = [&](size_t& pos) {
        while (pos < bytes.size() && static_cast<char>(bytes[pos]) != '\0') {
            pos++;
        }
        if (pos < bytes.size()) pos++; // Skip null terminator
    };

    skip_str(cursor); // Name
    skip_str(cursor); // Description
    skip_str(cursor); // Author
    cursor += 4;      // Addon version (int32)

    // File table loop
    std::vector<std::pair<std::string, uint64_t>> file_entries;

    while (cursor + 4 < bytes.size()) {
        uint32_t file_num = 0;
        std::memcpy(&file_num, bytes.data() + cursor, sizeof(uint32_t));
        cursor += 4;

        if (file_num == 0) break; // End of file table

        // Read file relative path
        size_t str_start = cursor;
        skip_str(cursor);
        std::string file_path(reinterpret_cast<const char*>(bytes.data() + str_start));

        uint64_t file_size = 0;
        if (cursor + 8 <= bytes.size()) {
            std::memcpy(&file_size, bytes.data() + cursor, sizeof(uint64_t));
            cursor += 8;
        }
        cursor += 4; // CRC32 (uint32)

        file_entries.push_back({file_path, file_size});
    }

    // Data blobs start immediately after file table
    size_t data_offset = cursor;

    for (const auto& [rel_path, fsize] : file_entries) {
        std::string vfs_uri = "vfs://" + container.mount_prefix + "/" + to_lower(rel_path);

        VFSEntry entry;
        entry.virtual_path = vfs_uri;
        entry.container_index = container_idx;
        entry.offset = data_offset;
        entry.disk_size = static_cast<size_t>(fsize);
        entry.uncompressed_size = static_cast<size_t>(fsize);
        entry.compression = CompressionType::None;

        m_index[vfs_uri] = entry;
        data_offset += static_cast<size_t>(fsize);
    }
}

void VFSManager::index_pbo(size_t container_idx) {
    const auto& container = m_containers[container_idx];
    auto bytes = container.mapped_file.bytes();
    size_t cursor = 0;

    struct RawPboHeader {
        std::string filename;
        parsers::rv_enfusion::PBOEntryFields fields;
    };
    std::vector<RawPboHeader> raw_headers;

    while (cursor < bytes.size()) {
        // Read null-terminated filename
        size_t str_start = cursor;
        while (cursor < bytes.size() && static_cast<char>(bytes[cursor]) != '\0') {
            cursor++;
        }
        if (cursor >= bytes.size()) break;

        std::string filename(reinterpret_cast<const char*>(bytes.data() + str_start));
        cursor++; // Skip null byte

        if (cursor + sizeof(parsers::rv_enfusion::PBOEntryFields) > bytes.size()) break;

        parsers::rv_enfusion::PBOEntryFields fields;
        std::memcpy(&fields, bytes.data() + cursor, sizeof(fields));
        cursor += sizeof(fields);

        // Terminating null entry marks end of header index
        if (filename.empty() && fields.packing_method == 0 && fields.original_size == 0) {
            break;
        }

        // Handle "Vers" version extension block
        if (fields.packing_method == parsers::rv_enfusion::kPboMagicVers) {
            cursor += fields.data_size; // Skip version key-value block
            continue;
        }

        raw_headers.push_back({filename, fields});
    }

    size_t payload_offset = cursor;

    for (const auto& raw : raw_headers) {
        std::string vfs_uri = "vfs://" + container.mount_prefix + "/" + to_lower(raw.filename);

        VFSEntry entry;
        entry.virtual_path = vfs_uri;
        entry.container_index = container_idx;
        entry.offset = payload_offset;
        entry.disk_size = static_cast<size_t>(raw.fields.data_size == 0 ? raw.fields.original_size : raw.fields.data_size);
        entry.uncompressed_size = static_cast<size_t>(raw.fields.original_size);

        if (raw.fields.packing_method == parsers::rv_enfusion::kPboMagicCprs) {
            entry.compression = CompressionType::LZSS_Bohemia;
        } else {
            entry.compression = CompressionType::None;
        }

        m_index[vfs_uri] = entry;
        payload_offset += entry.disk_size;
    }
}

bool VFSManager::file_exists(const String& vfs_uri) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string uri_std = to_lower(vfs_uri.utf8().get_data());
    return m_index.contains(uri_std);
}

PackedStringArray VFSManager::list_files(const String& prefix) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    PackedStringArray result;
    std::string prefix_std = to_lower(prefix.utf8().get_data());

    for (const auto& [uri, entry] : m_index) {
        if (prefix_std.empty() || uri.starts_with(prefix_std)) {
            result.append(String(uri.c_str()));
        }
    }
    return result;
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
        return std::nullopt;
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
        result.resize(static_cast<int64_t>(entry.disk_size));
        std::memcpy(result.ptrw(), entry_bytes.data(), entry_bytes.size());
    }

    return result;
}

} // namespace quebratsk::vfs
