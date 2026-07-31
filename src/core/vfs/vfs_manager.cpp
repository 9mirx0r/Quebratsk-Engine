#include "vfs_manager.h"
#include "../io/byte_reader.h"
#include "../../parsers/goldsrc/structs/wad3_structs.h"
#include "../../parsers/source1/structs/gma_structs.h"
#include "../../parsers/rv_enfusion/structs/pbo_structs.h"
#include "../../parsers/source2/vpk2_parser.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace quebratsk::vfs {

using namespace godot;

void VFSManager::_bind_methods() {
    ClassDB::bind_method(D_METHOD("mount_container", "vfs_prefix", "real_path"), &VFSManager::mount_container);
    ClassDB::bind_method(D_METHOD("mount_directory", "vfs_prefix", "real_dir"), &VFSManager::mount_directory);
    ClassDB::bind_method(D_METHOD("unmount", "vfs_prefix"), &VFSManager::unmount);
    ClassDB::bind_method(D_METHOD("file_exists", "vfs_uri"), &VFSManager::file_exists);
    ClassDB::bind_method(D_METHOD("list_files", "prefix"), &VFSManager::list_files, DEFVAL(""));
    ClassDB::bind_method(D_METHOD("read_file", "vfs_uri"), &VFSManager::read_file);
    ClassDB::bind_method(D_METHOD("get_file_size", "vfs_uri"), &VFSManager::get_file_size);
    ClassDB::bind_method(D_METHOD("get_mounts_info"), &VFSManager::get_mounts_info);
    ClassDB::bind_method(D_METHOD("scan_game_directory", "real_dir"), &VFSManager::scan_game_directory);
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

/// Read a NUL-terminated string, never scanning past the end of the buffer. Returns
/// nullopt when no terminator exists before EOF, which means the container is truncated.
///
/// Thin adapter over io::ByteReader so the cursor-style call sites below stay readable.
static std::optional<std::string> read_cstr_bounded(std::span<const std::byte> bytes, size_t& pos) {
    io::ByteReader reader(bytes, pos);
    auto out = reader.read_cstr();
    pos = reader.position();
    return out;
}

/// Overflow-safe replacement for `offset + length > size`.
static bool range_fits(size_t offset, size_t length, size_t size) {
    return io::range_fits(offset, length, 1, size);
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

    auto bytes = container.mapped_file.bytes();

    enum class Kind { WAD3, GMA, VPK, PBO };
    Kind kind;

    if (parsers::goldsrc::validate_wad3_header(bytes)) {
        container.engine = EngineNamespace::GoldSrc;
        kind = Kind::WAD3;
    } else if (parsers::source1::validate_gma_header(bytes)) {
        container.engine = EngineNamespace::Source1;
        kind = Kind::GMA;
    } else if (parsers::source2::validate_vpk_header(bytes)) {
        container.engine = EngineNamespace::Source1;
        kind = Kind::VPK;
    } else {
        // Real Virtuality / Enfusion PBO: starts with a NUL-terminated filename, so
        // there is no magic to test. Anything unrecognised lands here.
        container.engine = EngineNamespace::RV;
        kind = Kind::PBO;
    }

    const size_t container_idx = place_container(std::move(container));

    switch (kind) {
        case Kind::WAD3:
            index_wad3(container_idx);
            UtilityFunctions::print("[QuebratskVFS] Mounted GoldSrc WAD3: vfs://", String(prefix_std.c_str()), "/");
            break;
        case Kind::GMA:
            index_gma(container_idx);
            UtilityFunctions::print("[QuebratskVFS] Mounted Source 1 GMA: vfs://", String(prefix_std.c_str()), "/");
            break;
        case Kind::VPK:
            index_vpk(container_idx, path_std);
            UtilityFunctions::print("[QuebratskVFS] Mounted Source VPK: vfs://", String(prefix_std.c_str()), "/");
            break;
        case Kind::PBO:
            index_pbo(container_idx);
            UtilityFunctions::print("[QuebratskVFS] Mounted PBO Container: vfs://", String(prefix_std.c_str()), "/");
            break;
    }
    return true;
}

size_t VFSManager::place_container(MountedContainer&& container) {
    // Reuse a slot freed by unmount() instead of growing forever. Existing entries
    // reference containers by index, so slots are recycled, never erased.
    for (size_t i = 0; i < m_containers.size(); ++i) {
        if (!m_containers[i].mapped_file.is_valid()) {
            m_containers[i] = std::move(container);
            return i;
        }
    }
    m_containers.push_back(std::move(container));
    return m_containers.size() - 1;
}

void VFSManager::index_vpk(size_t container_idx, const std::string& dir_real_path) {
    // Copy what we need before mounting side archives: placing them can reallocate
    // m_containers and invalidate any reference held into it.
    const std::string prefix = m_containers[container_idx].mount_prefix;
    const auto dir_bytes = m_containers[container_idx].mapped_file.bytes();

    auto parsed = parsers::source2::VPK2Parser::parse_directory(dir_bytes);
    if (!parsed.has_value()) {
        UtilityFunctions::printerr("[QuebratskVFS] Malformed VPK directory: ",
                                   String(dir_real_path.c_str()));
        return;
    }

    // A VPK is a directory file plus numbered side archives holding the payload:
    // "hl2_misc_dir.vpk" is accompanied by "hl2_misc_000.vpk" and friends. Map each
    // referenced index to its own container so entries can address them normally.
    std::string base = dir_real_path;
    if (base.size() >= 8 && to_lower(base.substr(base.size() - 8)) == "_dir.vpk") {
        base.resize(base.size() - 8);
    }

    std::unordered_map<uint16_t, size_t> archive_containers;
    for (int32_t i = 0; i <= parsed->max_archive_index; ++i) {
        char suffix[16];
        std::snprintf(suffix, sizeof(suffix), "_%03d.vpk", i);

        auto side = MemoryMappedFile::open(base + suffix);
        if (!side.has_value()) {
            continue; // an unreferenced gap in the numbering is normal
        }

        MountedContainer mc;
        mc.engine = EngineNamespace::Source1;
        mc.real_path = base + suffix;
        mc.mount_prefix = prefix;
        mc.mapped_file = std::move(side.value());
        archive_containers[static_cast<uint16_t>(i)] = place_container(std::move(mc));
    }

    size_t indexed = 0;
    size_t skipped = 0;

    for (const auto& e : parsed->entries) {
        size_t owner = container_idx;
        if (!e.is_inline()) {
            const auto it = archive_containers.find(e.archive_index);
            if (it == archive_containers.end()) {
                ++skipped; // the side archive holding this file is not present
                continue;
            }
            owner = it->second;
        }

        // Entries whose payload lives entirely in the directory's preload block have a
        // zero length; serving the preload bytes keeps those files readable.
        size_t offset = static_cast<size_t>(e.offset);
        size_t length = static_cast<size_t>(e.length);
        if (length == 0 && e.preload_bytes > 0) {
            owner = container_idx;
            offset = static_cast<size_t>(e.preload_offset);
            length = e.preload_bytes;
        }

        if (owner >= m_containers.size()) { ++skipped; continue; }
        if (!range_fits(offset, length, m_containers[owner].mapped_file.bytes().size())) {
            ++skipped;
            continue;
        }

        const std::string vfs_uri = "vfs://" + prefix + "/" + to_lower(e.path);

        VFSEntry entry;
        entry.virtual_path = vfs_uri;
        entry.container_index = owner;
        entry.offset = offset;
        entry.disk_size = length;
        entry.uncompressed_size = length;
        entry.compression = CompressionType::None;

        m_index[vfs_uri] = std::move(entry);
        ++indexed;
    }

    UtilityFunctions::print("[QuebratskVFS]   VPK: ", static_cast<int64_t>(indexed),
                            " files across ", static_cast<int64_t>(archive_containers.size()),
                            " archives",
                            skipped ? String(" (" + String::num_int64(static_cast<int64_t>(skipped)) + " unavailable)") : String(""));
}

int64_t VFSManager::mount_directory(const String& vfs_prefix, const String& real_dir) {
    std::lock_guard<std::mutex> lock(m_mutex);

    const std::string prefix_std = normalize_prefix(to_lower(vfs_prefix.utf8().get_data()));
    const std::string dir_std = real_dir.utf8().get_data();

    std::error_code ec;
    const std::filesystem::path root(dir_std);
    if (!std::filesystem::is_directory(root, ec) || ec) {
        UtilityFunctions::printerr("[QuebratskVFS] Not a directory: ", real_dir);
        return 0;
    }

    int64_t indexed = 0;

    // The error_code overload keeps a permission-denied subdirectory from aborting the
    // whole walk (and, with exceptions disabled, from terminating the process).
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec) {
        UtilityFunctions::printerr("[QuebratskVFS] Cannot walk directory: ", real_dir);
        return 0;
    }

    const std::filesystem::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec) || ec) continue;

        const std::filesystem::path rel = std::filesystem::relative(it->path(), root, ec);
        if (ec) continue;

        std::string rel_str = rel.generic_string();
        std::replace(rel_str.begin(), rel_str.end(), '\\', '/');

        const std::string vfs_uri = "vfs://" + prefix_std + "/" + to_lower(rel_str);

        const auto size = std::filesystem::file_size(it->path(), ec);
        if (ec) continue;

        VFSEntry entry;
        entry.virtual_path = vfs_uri;
        entry.loose_path = it->path().string();
        entry.disk_size = static_cast<size_t>(size);
        entry.uncompressed_size = entry.disk_size;
        entry.compression = CompressionType::None;

        m_index[vfs_uri] = std::move(entry);
        ++indexed;
    }

    UtilityFunctions::print("[QuebratskVFS] Mounted directory (", indexed,
                            " files): vfs://", String(prefix_std.c_str()), "/");
    return indexed;
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

    const io::ByteReader reader(bytes);
    const auto header_span = reader.array_at<parsers::goldsrc::WAD3Header>(0, 1);
    if (header_span.empty()) return;
    const auto* header = header_span.data();

    // num_lumps and dir_offset are int32_t on disk; a negative value would cast to a
    // huge size_t and make the element-count arithmetic wrap. array_at() rejects the
    // whole range rather than trusting it.
    if (header->dir_offset <= 0 || header->num_lumps < 0) {
        return;
    }

    const auto lumps = reader.array_at<parsers::goldsrc::WAD3Lump>(
        static_cast<size_t>(header->dir_offset), static_cast<size_t>(header->num_lumps));
    if (lumps.empty()) return;

    for (const auto& lump : lumps) {
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

        // Handle the "Vers" properties block.
        //
        // What follows a Vers entry is a run of NUL-terminated key/value string pairs
        // ending in an empty string — NOT a data_size-sized blob. Skipping by data_size
        // (which is 0 here) left the cursor on the first property key, so "product" was
        // read as a filename and the whole index collapsed. Every DayZ and Arma PBO
        // starts with one of these, which is why none of them indexed.
        if (fields.packing_method == parsers::rv_enfusion::kPboMagicVers) {
            while (true) {
                auto prop = read_cstr_bounded(bytes, cursor);
                if (!prop) break;          // truncated
                if (prop->empty()) break;  // end of the property list
            }
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
    // Loose files are not memory-mapped, so there is no persistent span to hand out.
    // Callers must go through read_owned().
    if (entry.is_loose()) {
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

    // Loose files (mount_directory) are read on demand rather than mapped, so they must
    // be handled before the container paths.
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_index.find(to_lower(vfs_uri_str));
        if (it != m_index.end() && it->second.is_loose()) {
            const std::string path = it->second.loose_path;
            const size_t expected = it->second.disk_size;
            // Copy what we need, then drop the lock before touching the filesystem.
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                UtilityFunctions::printerr("[QuebratskVFS] Loose file vanished: ",
                                           String(path.c_str()));
                return owned;
            }
            owned.resize(expected);
            file.read(reinterpret_cast<char*>(owned.data()),
                      static_cast<std::streamsize>(expected));
            owned.resize(static_cast<size_t>(file.gcount()));
            return owned;
        }
    }

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
    // unique_lock, not lock_guard: the loose-file path below has to release the mutex
    // before delegating to read_owned(), which takes it again.
    std::unique_lock<std::mutex> lock(m_mutex);
    PackedByteArray result;

    std::string uri_std = to_lower(vfs_uri.utf8().get_data());
    auto it = m_index.find(uri_std);
    if (it == m_index.end()) {
        UtilityFunctions::printerr("[QuebratskVFS] File not found: ", vfs_uri);
        return result;
    }

    const auto& entry = it->second;
    if (entry.is_loose()) {
        // Loose entries have no container; defer to the on-demand reader.
        // read_owned() takes the mutex itself, so release it first.
        const std::string uri_copy = uri_std;
        lock.unlock();
        const std::vector<std::byte> bytes = read_owned(uri_copy);
        if (!bytes.empty()) {
            result.resize(static_cast<int64_t>(bytes.size()));
            std::memcpy(result.ptrw(), bytes.data(), bytes.size());
        }
        return result;
    }

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

Array VFSManager::get_mounts_info() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    // One pass over the index rather than one pass per mount. The nested form was
    // O(mounts x index): a Half-Life 2 plus Garry's Mod setup is 14 mounts over ~100k
    // entries, and this is called to refresh a UI panel.
    std::vector<int64_t> per_container(m_containers.size(), 0);
    for (const auto& [uri, entry] : m_index) {
        if (entry.container_index < per_container.size()) {
            ++per_container[entry.container_index];
        }
    }

    // A VPK is one _dir.vpk plus its numbered side archives, and index_vpk() mounts every
    // one of them under the SAME prefix. Reporting each container separately showed 7
    // rows for 3 mounts, most of them with a file_count of 0 or 1, because the entries
    // are attributed to whichever archive physically holds the bytes. Group by prefix:
    // that is the unit a user actually mounted and can unmount.
    Array mounts;
    std::unordered_map<std::string, int> row_of_prefix;

    for (size_t i = 0; i < m_containers.size(); ++i) {
        const auto& mount = m_containers[i];
        if (mount.mount_prefix.empty()) continue; // freed by unmount()

        if (auto it = row_of_prefix.find(mount.mount_prefix); it != row_of_prefix.end()) {
            Dictionary existing = mounts[it->second];
            existing["file_count"] = int64_t(existing["file_count"]) + per_container[i];
            existing["archive_count"] = int64_t(existing["archive_count"]) + 1;
            continue;
        }

        std::string eng_str = "Custom";
        switch (mount.engine) {
            case EngineNamespace::GoldSrc: eng_str = "GoldSrc"; break;
            case EngineNamespace::Source1: eng_str = "Source1"; break;
            case EngineNamespace::RV: eng_str = "RealVirtuality"; break;
            case EngineNamespace::BSP: eng_str = "BSP"; break;
            default: break;
        }

        Dictionary info;
        info["prefix"] = String(mount.mount_prefix.c_str());
        // The first container under a prefix is the one the caller named; the side
        // archives are placed after it.
        info["real_path"] = String(mount.real_path.c_str());
        info["engine"] = String(eng_str.c_str());
        info["file_count"] = per_container[i];
        info["archive_count"] = int64_t(1);  // real files backing this mount

        row_of_prefix[mount.mount_prefix] = static_cast<int>(mounts.size());
        mounts.append(info);
    }
    return mounts;
}

Dictionary VFSManager::scan_game_directory(const String& real_dir) const {
    Dictionary scan_result;
    std::string root = real_dir.utf8().get_data();
    std::error_code ec;

    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        scan_result["error"] = "Directory does not exist or is unreadable";
        return scan_result;
    }

    int64_t total_archives = 0;
    int64_t loose_models = 0;
    int64_t loose_maps = 0;
    int64_t loose_textures = 0;

    Array archives_found;

    // Explicit iterator with increment(ec) rather than a range-for. The range-for calls
    // operator++(), which THROWS on an I/O error, and godot-cpp is built with exceptions
    // disabled — so a disconnected drive or an over-long path partway through a scan
    // would call terminate() and take the editor down. skip_permission_denied covers
    // only the most common case, not all of them.
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec) {
        scan_result["error"] = "Directory could not be opened for scanning";
        return scan_result;
    }

    const std::filesystem::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) break; // report what was counted so far rather than nothing

        std::error_code file_ec;
        if (!it->is_regular_file(file_ec) || file_ec) continue;

        std::string ext = it->path().extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (ext == ".wad" || ext == ".vpk" || ext == ".gma" || ext == ".pbo" ||
            ext == ".pak" || ext == ".bundle") {
            // Side archives are not separately mountable; the _dir.vpk pulls them in.
            const std::string name = to_lower(it->path().filename().string());
            if (ext == ".vpk" && !name.ends_with("_dir.vpk")) continue;

            total_archives++;
            archives_found.append(String(it->path().string().c_str()));
        } else if (ext == ".mdl" || ext == ".p3d") {
            loose_models++;
        } else if (ext == ".bsp" || ext == ".wrp") {
            loose_maps++;
        } else if (ext == ".vtf" || ext == ".paa" || ext == ".png" || ext == ".jpg" ||
                   ext == ".tga") {
            loose_textures++;
        }
    }

    scan_result["total_archives"] = total_archives;
    // Deliberately named "loose_": these count files sitting on disk, NOT the contents of
    // the archives. A modern Source game keeps everything inside VPKs, so scanning
    // GarrysMod/ honestly reports 1 loose model — the answer to "what can I import here"
    // is the archive list, and the caller must mount those to see inside.
    scan_result["loose_models"] = loose_models;
    scan_result["loose_maps"] = loose_maps;
    scan_result["loose_textures"] = loose_textures;
    scan_result["archives"] = archives_found;

    return scan_result;
}

} // namespace quebratsk::vfs
