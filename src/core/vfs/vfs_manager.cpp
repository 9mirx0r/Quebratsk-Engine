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
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

/// Normalize a user-supplied mount prefix: strip the "vfs://" scheme and any
/// trailing slash. mount_container() and unmount() must agree on this or unmount
/// silently matches nothing.
static std::string normalize_prefix(std::string prefix) {
    if (prefix.starts_with("vfs://")) {
        prefix = prefix.substr(6);
    }
    if (prefix.ends_with("/")) {
        prefix.pop_back();
    }
    return prefix;
}

/// Read a NUL-terminated string from `bytes` starting at `pos`, never scanning past
/// the end of the buffer. Returns nullopt when no terminator exists before EOF, which
/// means the container is truncated. Constructing std::string from a raw pointer here
/// would run off the end of the memory mapping.
static std::optional<std::string> read_cstr_bounded(std::span<const std::byte> bytes, size_t& pos) {
    const size_t start = pos;
    while (pos < bytes.size() && static_cast<char>(bytes[pos]) != '\0') {
        ++pos;
    }
    if (pos >= bytes.size()) {
        return std::nullopt;
    }
    std::string out(reinterpret_cast<const char*>(bytes.data() + start), pos - start);
    ++pos; // consume the terminator
    return out;
}

/// Overflow-safe replacement for `offset + length > size`.
static bool range_fits(size_t offset, size_t length, size_t size) {
    return offset <= size && length <= size - offset;
}

bool VFSManager::mount_container(const String& vfs_prefix, const String& real_path) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string prefix_std = normalize_prefix(to_lower(vfs_prefix.utf8().get_data()));
    std::string path_std = real_path.utf8().get_data();

    auto mmap_res = MemoryMappedFile::open(path_std);
    if (!mmap_res.has_value()) {
        UtilityFunctions::printerr("[QuebratskVFS] Failed to memory map container: ", real_path);
        return false;
    }

    MountedContainer container;
    container.real_path = path_std;
    container.mount_prefix = prefix_std;
    container.mapped_file = std::move(mmap_res.value());

    // Reuse a slot freed by unmount() instead of growing forever. Existing entries
    // reference containers by index, so slots are recycled, never erased.
    size_t container_idx = m_containers.size();
    for (size_t i = 0; i < m_containers.size(); ++i) {
        if (!m_containers[i].mapped_file.is_valid()) {
            container_idx = i;
            break;
        }
    }

    auto bytes = container.mapped_file.bytes();

    enum class Kind { WAD3, GMA, PBO };
    Kind kind;

    if (parsers::goldsrc::validate_wad3_header(bytes)) {
        container.engine = EngineNamespace::GoldSrc;
        kind = Kind::WAD3;
    } else if (parsers::source1::validate_gma_header(bytes)) {
        container.engine = EngineNamespace::Source1;
        kind = Kind::GMA;
    } else {
        // Real Virtuality / Enfusion PBO: starts with a NUL-terminated filename, so
        // there is no magic to test. Anything unrecognised lands here.
        container.engine = EngineNamespace::RV;
        kind = Kind::PBO;
    }

    if (container_idx < m_containers.size()) {
        m_containers[container_idx] = std::move(container);
    } else {
        m_containers.push_back(std::move(container));
    }

    switch (kind) {
        case Kind::WAD3:
            index_wad3(container_idx);
            UtilityFunctions::print("[QuebratskVFS] Mounted GoldSrc WAD3: vfs://", String(prefix_std.c_str()), "/");
            break;
        case Kind::GMA:
            index_gma(container_idx);
            UtilityFunctions::print("[QuebratskVFS] Mounted Source 1 GMA: vfs://", String(prefix_std.c_str()), "/");
            break;
        case Kind::PBO:
            index_pbo(container_idx);
            UtilityFunctions::print("[QuebratskVFS] Mounted PBO Container: vfs://", String(prefix_std.c_str()), "/");
            break;
    }
    return true;
}

void VFSManager::unmount(const String& vfs_prefix) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Must normalize exactly like mount_container(), otherwise unmount("vfs://cs16")
    // builds "vfs://vfs://cs16/" and matches nothing.
    const std::string prefix_std = normalize_prefix(to_lower(vfs_prefix.utf8().get_data()));
    const std::string uri_prefix = "vfs://" + prefix_std + "/";

    for (auto it = m_index.begin(); it != m_index.end(); ) {
        if (it->second.virtual_path.starts_with(uri_prefix)) {
            it = m_index.erase(it);
        } else {
            ++it;
        }
    }

    // Previously the index was cleared but the mapping was never released: the OS file
    // handle stayed open (keeping the archive locked on Windows) and the address-space
    // reservation leaked. Close the mapping and mark the slot reusable. The slot itself
    // is kept so surviving entries' container_index values stay valid.
    for (auto& c : m_containers) {
        if (c.mapped_file.is_valid() && c.mount_prefix == prefix_std) {
            c.mapped_file.close();
            c.mount_prefix.clear();
            c.real_path.clear();
        }
    }
}

void VFSManager::index_wad3(size_t container_idx) {
    const auto& container = m_containers[container_idx];
    auto bytes = container.mapped_file.bytes();

    if (bytes.size() < sizeof(parsers::goldsrc::WAD3Header)) {
        return;
    }

    auto* header = reinterpret_cast<const parsers::goldsrc::WAD3Header*>(bytes.data());
    // num_lumps and dir_offset are int32_t on disk; a negative value would cast to a
    // huge size_t and make the multiplication below wrap.
    if (header->dir_offset <= 0 || header->num_lumps < 0) {
        return;
    }

    const size_t num_lumps = static_cast<size_t>(header->num_lumps);
    const size_t dir_ofs = static_cast<size_t>(header->dir_offset);
    constexpr size_t kLumpSize = sizeof(parsers::goldsrc::WAD3Lump);

    if (dir_ofs > bytes.size() || num_lumps > (bytes.size() - dir_ofs) / kLumpSize) {
        return;
    }

    auto* lumps = reinterpret_cast<const parsers::goldsrc::WAD3Lump*>(bytes.data() + dir_ofs);

    for (size_t i = 0; i < num_lumps; ++i) {
        const auto& lump = lumps[i];
        if (lump.file_pos < 0 || lump.disk_size < 0 || lump.uncompressed_size < 0) {
            continue;
        }

        const size_t offset = static_cast<size_t>(lump.file_pos);
        const size_t disk_size = static_cast<size_t>(lump.disk_size);
        if (!range_fits(offset, disk_size, bytes.size())) {
            continue; // corrupt directory entry, skip rather than index a bad range
        }

        std::string lump_name(lump.name, strnlen(lump.name, sizeof(lump.name)));
        std::string vfs_uri = "vfs://" + container.mount_prefix + "/" + to_lower(lump_name);

        VFSEntry entry;
        entry.virtual_path = vfs_uri;
        entry.container_index = container_idx;
        entry.offset = offset;
        entry.disk_size = disk_size;
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

    // Name, Description, Author. read_cstr_bounded() fails on a missing terminator
    // instead of letting std::string(const char*) scan past the end of the mapping.
    if (!read_cstr_bounded(bytes, cursor)) return;
    if (!read_cstr_bounded(bytes, cursor)) return;
    if (!read_cstr_bounded(bytes, cursor)) return;

    if (!range_fits(cursor, 4, bytes.size())) return;
    cursor += 4; // Addon version (int32)

    // File table loop
    std::vector<std::pair<std::string, uint64_t>> file_entries;

    while (range_fits(cursor, 4, bytes.size())) {
        uint32_t file_num = 0;
        std::memcpy(&file_num, bytes.data() + cursor, sizeof(uint32_t));
        cursor += 4;

        if (file_num == 0) break; // End of file table

        auto file_path = read_cstr_bounded(bytes, cursor);
        if (!file_path) break; // truncated table

        if (!range_fits(cursor, 12, bytes.size())) break; // int64 size + uint32 CRC

        uint64_t file_size = 0;
        std::memcpy(&file_size, bytes.data() + cursor, sizeof(uint64_t));
        cursor += 8;
        cursor += 4; // CRC32 (uint32)

        file_entries.push_back({std::move(*file_path), file_size});
    }

    // Data blobs start immediately after file table
    size_t data_offset = cursor;

    for (const auto& [rel_path, fsize] : file_entries) {
        // file_size is attacker-controlled. Validating each blob here keeps the stored
        // offsets inside the mapping, so read_file() can never build an out-of-range span.
        if (fsize > bytes.size() || !range_fits(data_offset, static_cast<size_t>(fsize), bytes.size())) {
            UtilityFunctions::printerr("[QuebratskVFS] GMA entry out of bounds, stopping index at: ",
                                       String(rel_path.c_str()));
            break;
        }

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
        auto filename_opt = read_cstr_bounded(bytes, cursor);
        if (!filename_opt) break;
        std::string filename = std::move(*filename_opt);

        if (!range_fits(cursor, sizeof(parsers::rv_enfusion::PBOEntryFields), bytes.size())) break;

        parsers::rv_enfusion::PBOEntryFields fields;
        std::memcpy(&fields, bytes.data() + cursor, sizeof(fields));
        cursor += sizeof(fields);

        // Terminating null entry marks end of header index
        if (filename.empty() && fields.packing_method == 0 && fields.original_size == 0) {
            break;
        }

        // Handle "Vers" version extension block
        if (fields.packing_method == parsers::rv_enfusion::kPboMagicVers) {
            if (!range_fits(cursor, fields.data_size, bytes.size())) break;
            cursor += fields.data_size; // Skip version key-value block
            continue;
        }

        raw_headers.push_back({filename, fields});
    }

    size_t payload_offset = cursor;

    for (const auto& raw : raw_headers) {
        const size_t disk_size = static_cast<size_t>(
            raw.fields.data_size == 0 ? raw.fields.original_size : raw.fields.data_size);

        // Keep every indexed offset inside the mapping so readers cannot build an
        // out-of-range span later.
        if (!range_fits(payload_offset, disk_size, bytes.size())) {
            UtilityFunctions::printerr("[QuebratskVFS] PBO entry out of bounds, stopping index at: ",
                                       String(raw.filename.c_str()));
            break;
        }

        std::string vfs_uri = "vfs://" + container.mount_prefix + "/" + to_lower(raw.filename);

        VFSEntry entry;
        entry.virtual_path = vfs_uri;
        entry.container_index = container_idx;
        entry.offset = payload_offset;
        entry.disk_size = disk_size;
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

    if (entry.container_index >= m_containers.size()) return std::nullopt;
    const auto& container = m_containers[entry.container_index];
    if (!container.mapped_file.is_valid()) return std::nullopt; // unmounted

    auto mapped_bytes = container.mapped_file.bytes();

    // `offset + disk_size` overflows for crafted archives and wraps below the size,
    // letting subspan() run with offset > size(). Compare with subtraction instead.
    if (!range_fits(entry.offset, entry.disk_size, mapped_bytes.size())) {
        return std::nullopt;
    }

    return mapped_bytes.subspan(entry.offset, entry.disk_size);
}

std::string VFSManager::find_by_suffix(const std::string& lowercase_suffix) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (lowercase_suffix.empty()) return {};

    for (const auto& [uri, entry] : m_index) {
        if (uri.size() >= lowercase_suffix.size() && uri.ends_with(lowercase_suffix)) {
            return uri;
        }
    }
    return {};
}

std::vector<std::byte> VFSManager::read_owned(const std::string& vfs_uri_str) const {
    std::vector<std::byte> owned;

    // Uncompressed entries can be copied straight out of the mapping.
    if (auto raw = get_raw_span(vfs_uri_str); raw.has_value()) {
        owned.assign(raw->begin(), raw->end());
        return owned;
    }

    // Compressed entries go through the decompressing path.
    const PackedByteArray bytes = read_file(String(vfs_uri_str.c_str()));
    if (bytes.is_empty()) return owned;

    owned.resize(static_cast<size_t>(bytes.size()));
    std::memcpy(owned.data(), bytes.ptr(), owned.size());
    return owned;
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
    if (entry.container_index >= m_containers.size()) {
        UtilityFunctions::printerr("[QuebratskVFS] Stale container index for: ", vfs_uri);
        return result;
    }

    const auto& container = m_containers[entry.container_index];
    if (!container.mapped_file.is_valid()) {
        UtilityFunctions::printerr("[QuebratskVFS] Container was unmounted for: ", vfs_uri);
        return result;
    }

    auto mapped_bytes = container.mapped_file.bytes();

    // Overflow-safe: `offset + disk_size` can wrap for a crafted archive.
    if (!range_fits(entry.offset, entry.disk_size, mapped_bytes.size())) {
        UtilityFunctions::printerr("[QuebratskVFS] Out of bounds entry for: ", vfs_uri);
        return result;
    }

    std::span<const std::byte> entry_bytes = mapped_bytes.subspan(entry.offset, entry.disk_size);

    if (entry.compression == CompressionType::LZSS_Bohemia) {
        auto decomp_res = LZSSDecompressor::decompress(entry_bytes, entry.uncompressed_size);
        if (decomp_res.has_value()) {
            result.resize(static_cast<int64_t>(decomp_res->size()));
            std::memcpy(result.ptrw(), decomp_res->data(), decomp_res->size());
        } else {
            // The decompressor no longer zero-pads a short result, so surface the
            // failure instead of handing back a silently blank buffer.
            UtilityFunctions::printerr("[QuebratskVFS] LZSS decompression failed for: ", vfs_uri);
        }
    } else {
        result.resize(static_cast<int64_t>(entry.disk_size));
        std::memcpy(result.ptrw(), entry_bytes.data(), entry_bytes.size());
    }

    return result;
}

} // namespace quebratsk::vfs
