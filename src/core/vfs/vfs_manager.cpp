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
    ClassDB::bind_method(D_METHOD("find_files", "needle", "extensions", "exclude",
                                  "prefixes", "limit", "games"),
                         &VFSManager::find_files,
                         DEFVAL(PackedStringArray()), DEFVAL(PackedStringArray()),
                         DEFVAL(PackedStringArray()), DEFVAL(0),
                         DEFVAL(PackedStringArray()));
    ClassDB::bind_method(D_METHOD("read_file", "vfs_uri"), &VFSManager::read_file);
    ClassDB::bind_method(D_METHOD("get_file_size", "vfs_uri"), &VFSManager::get_file_size);
    ClassDB::bind_method(D_METHOD("get_mounts_info"), &VFSManager::get_mounts_info);
    ClassDB::bind_method(D_METHOD("scan_game_directory", "real_dir"), &VFSManager::scan_game_directory);
    ClassDB::bind_method(D_METHOD("get_game_of", "vfs_uri"), &VFSManager::get_game_of);
    ClassDB::bind_method(D_METHOD("resolve_reference", "fragment", "origin_uri"),
                         &VFSManager::resolve_reference, DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("get_game_search_order"), &VFSManager::get_game_search_order);
}

/// A path as it is filed in the index: lowercase, forward slashes.
///
/// Real Virtuality PBOs store their paths with backslashes, and index_pbo() was the one
/// place that did not convert them. Everything downstream that normalises a path before
/// looking it up then missed: find_by_suffix() could not match a nested file, the dock's
/// folder categories never matched Arma or DayZ content, and the texture loader turned
/// a backslash path into a slash one and was told it did not exist. Only files sitting at
/// the root of a PBO, with no separator to disagree about, ever resolved.
static std::string to_index_path(std::string_view str) {
    std::string result(str);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
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

void VFSManager::adopt_game(MountedContainer& container) {
    if (container.real_path.empty()) return;

    std::error_code ec;
    std::filesystem::path dir(container.real_path);
    if (std::filesystem::is_regular_file(dir, ec)) dir = dir.parent_path();
    const std::string key = dir.string();

    if (const auto cached = m_game_of_dir.find(key); cached != m_game_of_dir.end()) {
        container.game_id = cached->second.first;
        container.game_name = cached->second.second;
        return;
    }

    std::string id;
    std::string name;
    if (auto manifest = find_owning_game(container.real_path); manifest.has_value()) {
        id = manifest->id;
        name = manifest->name;
        // First manifest wins. The same game can be reached down two paths, and its own
        // declaration is the one that counts either way.
        m_game_fallbacks.try_emplace(id, manifest->fallbacks);
        m_game_names.try_emplace(id, manifest->title.empty() ? name : manifest->title);
    }
    m_game_of_dir.emplace(key, std::make_pair(id, name));
    container.game_id = std::move(id);
    container.game_name = std::move(name);
}

uint16_t VFSManager::intern_game(const std::string& id) {
    if (id.empty()) return 0;
    if (const auto it = m_game_slots.find(id); it != m_game_slots.end()) return it->second;

    // Two bytes per entry only works while the table stays small, and it is: one slot per
    // game installed. Overflowing it means falling back to the container's answer, which is
    // the behaviour this replaced rather than a new failure.
    if (m_game_ids.size() >= 0xFFFF) return 0;

    const uint16_t slot = static_cast<uint16_t>(m_game_ids.size());
    m_game_ids.push_back(id);
    m_game_slots.emplace(id, slot);
    return slot;
}

const std::string& VFSManager::game_id_of(const VFSEntry& entry) const {
    static const std::string none;
    if (entry.game != 0 && entry.game < m_game_ids.size()) return m_game_ids[entry.game];
    if (entry.container_index < m_containers.size()) {
        return m_containers[entry.container_index].game_id;
    }
    return none;
}

std::vector<std::string> VFSManager::search_order(const VFSEntry& entry) const {
    std::vector<std::string> order;
    const std::string& game = game_id_of(entry);
    if (game.empty()) return order;

    order.push_back(game);
    if (const auto it = m_game_fallbacks.find(game); it != m_game_fallbacks.end()) {
        order.insert(order.end(), it->second.begin(), it->second.end());
    }
    return order;
}

std::string VFSManager::game_of(const std::string& vfs_uri) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_index.find(to_lower(vfs_uri));
    if (it == m_index.end()) return {};
    return game_id_of(it->second);
}

size_t VFSManager::place_container(MountedContainer&& container) {
    container.occupied = true;
    container.order = m_mount_counter++;
    adopt_game(container);

    // Reuse a slot freed by unmount() instead of growing forever. Existing entries
    // reference containers by index, so slots are recycled, never erased.
    for (size_t i = 0; i < m_containers.size(); ++i) {
        if (!m_containers[i].occupied) {
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

    // Size the index for what is about to go in it. An unordered_map grown from empty to
    // 60,584 entries rehashes roughly seventeen times on the way, and every rehash walks
    // and reinserts everything already there.
    m_index.reserve(m_index.size() + parsed->entries.size());

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

        std::string vfs_uri = "vfs://" + prefix + "/" + to_index_path(e.path);

        VFSEntry entry;
        entry.container_index = owner;
        entry.offset = offset;
        entry.disk_size = length;
        entry.uncompressed_size = length;
        entry.compression = CompressionType::None;

        m_index.insert_or_assign(std::move(vfs_uri), std::move(entry));
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

    // A directory mount is a first-class container even though there is nothing to
    // memory-map. Without a slot of its own, every entry below kept container_index at
    // its default of 0 and get_mounts_info() reported the files under whichever archive
    // happened to occupy that slot — so the directory never appeared as a mount, and the
    // dock could not persist or unmount it.
    MountedContainer container;
    container.real_path = dir_std;
    container.mount_prefix = prefix_std;
    container.engine = EngineNamespace::Custom;
    container.is_directory = true;
    const size_t container_idx = place_container(std::move(container));

    int64_t indexed = 0;

    // The error_code overload keeps a permission-denied subdirectory from aborting the
    // whole walk (and, with exceptions disabled, from terminating the process).
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec) {
        UtilityFunctions::printerr("[QuebratskVFS] Cannot walk directory: ", real_dir);
        return 0;
    }

    // Resolved per directory rather than per file. The walk yields a folder's files together,
    // so the same answer serves a whole directory and reading a manifest stays rare.
    std::filesystem::path last_dir;
    uint16_t last_game = 0;
    bool have_last = false;

    const std::filesystem::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec) || ec) continue;

        const std::filesystem::path parent = it->path().parent_path();
        if (!have_last || parent != last_dir) {
            MountedContainer probe;
            probe.real_path = parent.string();
            adopt_game(probe);
            last_game = intern_game(probe.game_id);
            last_dir = parent;
            have_last = true;
        }

        // Lexically, not std::filesystem::relative(): that one resolves both paths through
        // weakly_canonical, touching the filesystem once per file to chase symlinks that
        // cannot be there, since the walk is already rooted at `root`. Subtracting a known
        // prefix needs no syscall at all.
        const std::string rel_str = it->path().lexically_relative(root).generic_string();

        std::string vfs_uri = "vfs://" + prefix_std + "/" + to_index_path(rel_str);

        // From the directory_entry, which cached it during enumeration. Asking
        // std::filesystem::file_size() for the path re-opens the file to learn something
        // the walk was already told.
        const auto size = it->file_size(ec);
        if (ec) continue;

        VFSEntry entry;
        entry.container_index = container_idx;
        entry.game = last_game;
        entry.loose_path = it->path().string();
        entry.disk_size = static_cast<size_t>(size);
        entry.uncompressed_size = entry.disk_size;
        entry.compression = CompressionType::None;

        m_index.insert_or_assign(std::move(vfs_uri), std::move(entry));
        ++indexed;
    }

    if (indexed == 0) {
        // Nothing was found, so do not leave an empty mount behind for the UI to show.
        m_containers[container_idx] = MountedContainer{};
        return 0;
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
        if (it->first.starts_with(uri_prefix)) {
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
        if (c.occupied && c.mount_prefix == prefix_std) {
            if (c.mapped_file.is_valid()) {
                c.mapped_file.close();
            }
            c.mount_prefix.clear();
            c.real_path.clear();
            c.is_directory = false;
            c.occupied = false;
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

    // Size the index for what is about to go in it. An unordered_map grown from empty to
    // 60,584 entries rehashes roughly seventeen times on the way, and every rehash walks
    // and reinserts everything already there.
    m_index.reserve(m_index.size() + lumps.size());

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
        std::string vfs_uri = "vfs://" + container.mount_prefix + "/" + to_index_path(lump_name);

        VFSEntry entry;
        entry.container_index = container_idx;
        entry.offset = offset;
        entry.disk_size = disk_size;
        entry.uncompressed_size = static_cast<size_t>(lump.uncompressed_size);
        entry.compression = CompressionType::None;

        m_index.insert_or_assign(std::move(vfs_uri), std::move(entry));
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

    // Size the index for what is about to go in it. An unordered_map grown from empty to
    // 60,584 entries rehashes roughly seventeen times on the way, and every rehash walks
    // and reinserts everything already there.
    m_index.reserve(m_index.size() + file_entries.size());

    for (const auto& [rel_path, fsize] : file_entries) {
        // file_size is attacker-controlled. Validating each blob here keeps the stored
        // offsets inside the mapping, so read_file() can never build an out-of-range span.
        if (fsize > bytes.size() || !range_fits(data_offset, static_cast<size_t>(fsize), bytes.size())) {
            UtilityFunctions::printerr("[QuebratskVFS] GMA entry out of bounds, stopping index at: ",
                                       String(rel_path.c_str()));
            break;
        }

        std::string vfs_uri = "vfs://" + container.mount_prefix + "/" + to_index_path(rel_path);

        VFSEntry entry;
        entry.container_index = container_idx;
        entry.offset = data_offset;
        entry.disk_size = static_cast<size_t>(fsize);
        entry.uncompressed_size = static_cast<size_t>(fsize);
        entry.compression = CompressionType::None;

        m_index.insert_or_assign(std::move(vfs_uri), std::move(entry));
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

    // Size the index for what is about to go in it. An unordered_map grown from empty to
    // 60,584 entries rehashes roughly seventeen times on the way, and every rehash walks
    // and reinserts everything already there.
    m_index.reserve(m_index.size() + raw_headers.size());

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

        std::string vfs_uri = "vfs://" + container.mount_prefix + "/" + to_index_path(raw.filename);

        VFSEntry entry;
        entry.container_index = container_idx;
        entry.offset = payload_offset;
        entry.disk_size = disk_size;
        // original_size is only meaningful for a compressed entry; an uncompressed one
        // leaves it at zero and its stored size is its real size. Taking it at face value
        // made get_file_size() report 0 for most of a DayZ install, including a 213 MB
        // terrain, while read_file() on the same entry returned every byte.
        entry.uncompressed_size = raw.fields.original_size != 0
                                      ? static_cast<size_t>(raw.fields.original_size)
                                      : disk_size;

        if (raw.fields.packing_method == parsers::rv_enfusion::kPboMagicCprs) {
            entry.compression = CompressionType::LZSS_Bohemia;
        } else {
            entry.compression = CompressionType::None;
        }

        m_index.insert_or_assign(std::move(vfs_uri), std::move(entry));
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

Dictionary VFSManager::find_files(const String& needle,
                                  const PackedStringArray& extensions,
                                  const PackedStringArray& exclude,
                                  const PackedStringArray& prefixes,
                                  int64_t limit,
                                  const PackedStringArray& games) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    // The index is already lowercase, so the needle is lowered once here rather than
    // per entry, and no per-entry String is constructed until something matches.
    const std::string want = to_lower(needle.utf8().get_data());

    auto lowered = [](const PackedStringArray& in) {
        std::vector<std::string> out;
        out.reserve(static_cast<size_t>(in.size()));
        for (int i = 0; i < in.size(); ++i) {
            out.push_back(to_lower(in[i].utf8().get_data()));
        }
        return out;
    };
    const std::vector<std::string> wanted_ext = lowered(extensions);
    const std::vector<std::string> unwanted_ext = lowered(exclude);

    // "vfs://name/" rather than the bare prefix, so a mount called "hl2" cannot also match
    // one called "hl2_sound".
    std::vector<std::string> wanted_mounts;
    wanted_mounts.reserve(static_cast<size_t>(prefixes.size()));
    for (int i = 0; i < prefixes.size(); ++i) {
        wanted_mounts.push_back("vfs://" + normalize_prefix(to_lower(prefixes[i].utf8().get_data()))
                                + "/");
    }

    // Collecting matches before choosing a page is what makes the page mean anything: the
    // index is a hash map, so iteration order is whatever the bucket layout gives, and it
    // changes when the map is resized. "The first 400 of 41,969" needs there to be a first.
    //
    // The filename offset is carried alongside rather than recomputed, because sorting
    // compares each entry many times and scanning back for the last slash on every one of
    // those comparisons is the expensive way to reach the same answer. The pointer stays
    // valid: the lock is held and nothing is inserted here.
    struct Match {
        const std::string* uri;
        uint32_t name_at;
    };
    std::vector<Match> matches;
    matches.reserve(m_index.size());

    std::vector<std::string> wanted_games;
    wanted_games.reserve(static_cast<size_t>(games.size()));
    for (int i = 0; i < games.size(); ++i) {
        wanted_games.push_back(to_lower(games[i].utf8().get_data()));
    }

    for (const auto& [uri, entry] : m_index) {
        if (!wanted_games.empty()) {
            const std::string& game = game_id_of(entry);
            bool from_wanted = false;
            for (const auto& wanted : wanted_games) {
                if (game == wanted) { from_wanted = true; break; }
            }
            if (!from_wanted) continue;
        }

        if (!wanted_mounts.empty()) {
            bool from_wanted = false;
            for (const auto& mount : wanted_mounts) {
                if (uri.starts_with(mount)) { from_wanted = true; break; }
            }
            if (!from_wanted) continue;
        }

        std::string_view ext;
        if (const size_t dot = uri.find_last_of('.'); dot != std::string::npos) {
            ext = std::string_view(uri.data() + dot + 1, uri.size() - dot - 1);
        }

        if (!unwanted_ext.empty()) {
            bool skip = false;
            for (const auto& candidate : unwanted_ext) {
                if (ext == candidate) { skip = true; break; }
            }
            if (skip) continue;
        }

        if (!wanted_ext.empty()) {
            if (ext.empty()) continue;
            bool ok = false;
            for (const auto& candidate : wanted_ext) {
                if (ext == candidate) { ok = true; break; }
            }
            if (!ok) continue;
        }

        if (!want.empty() && uri.find(want) == std::string::npos) continue;

        const size_t slash = uri.find_last_of('/');
        matches.push_back({&uri, static_cast<uint32_t>(slash == std::string::npos ? 0
                                                                                 : slash + 1)});
    }

    // By filename first, full path second. A user searching "police" wants the two games'
    // police.mdl side by side, which sorting on the whole URI would not give: that groups
    // by mount prefix, so a 400-row page would come entirely from whichever game sorts
    // first and the second game's matches would be invisible.
    auto by_name = [](const Match& a, const Match& b) {
        const std::string_view an(a.uri->data() + a.name_at, a.uri->size() - a.name_at);
        const std::string_view bn(b.uri->data() + b.name_at, b.uri->size() - b.name_at);
        return an != bn ? an < bn : *a.uri < *b.uri;
    };

    // Only the page is ordered. Sorting all 41,969 matches to show 400 of them is work
    // thrown away, and partial_sort is O(n log page) against O(n log n).
    const size_t take = (limit <= 0)
                            ? matches.size()
                            : std::min(static_cast<size_t>(limit), matches.size());
    std::partial_sort(matches.begin(),
                      matches.begin() + static_cast<std::ptrdiff_t>(take),
                      matches.end(), by_name);

    PackedStringArray files;
    files.resize(static_cast<int64_t>(take));
    for (size_t i = 0; i < take; ++i) {
        files.set(static_cast<int64_t>(i), String(matches[i].uri->c_str()));
    }

    Dictionary out;
    out["files"] = files;
    // The count is every match, not the page: a UI needs it to say how much it is not
    // showing.
    out["total"] = static_cast<int64_t>(matches.size());
    return out;
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

std::string VFSManager::find_by_suffix(const std::string& lowercase_suffix,
                                       const std::string& origin_uri) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (lowercase_suffix.empty()) return {};

    constexpr size_t kNoContainer = static_cast<size_t>(-1);
    size_t origin_container = kNoContainer;
    std::vector<std::string> order;

    std::string origin_game;
    if (!origin_uri.empty()) {
        if (const auto it = m_index.find(to_lower(origin_uri)); it != m_index.end()) {
            origin_container = it->second.container_index;
            origin_game = game_id_of(it->second);
            order = search_order(it->second);
        }
    }

    // Lower is better: the asking asset's own archive, then its game, then each game its
    // game falls back to, then anything else. With no origin every candidate scores the
    // same and the mount order decides, which is still an answer that does not change.
    auto rank_of = [&](const VFSEntry& entry) -> size_t {
        const std::string& game = game_id_of(entry);

        // Same archive is best, but only when the archive stands for one game. A directory
        // mount can hold four of them at once, and there the game is what separates them.
        if (entry.container_index == origin_container && game == origin_game) return 0;

        for (size_t i = 0; i < order.size(); ++i) {
            if (order[i] == game) return i + 1;
        }
        return order.size() + 1;
    };

    const std::string* best = nullptr;
    size_t best_rank = 0;
    size_t best_order = 0;

    for (const auto& [uri, entry] : m_index) {
        if (!uri.ends_with(lowercase_suffix)) continue;

        const size_t rank = rank_of(entry);
        // Nothing can beat the asking asset's own archive, so the rest of the index does
        // not need looking at. This is the common case and it is what keeps a full scan
        // from being the price of every texture in a model.
        if (rank == 0) return uri;

        const size_t mount_order = entry.container_index < m_containers.size()
                                       ? m_containers[entry.container_index].order
                                       : static_cast<size_t>(-1);

        const bool better = best == nullptr || rank < best_rank
                         || (rank == best_rank && mount_order < best_order)
                         || (rank == best_rank && mount_order == best_order && uri < *best);
        if (better) {
            best = &uri;
            best_rank = rank;
            best_order = mount_order;
        }
    }
    return best != nullptr ? *best : std::string{};
}

String VFSManager::get_game_of(const String& vfs_uri) const {
    return String(game_of(vfs_uri.utf8().get_data()).c_str());
}

String VFSManager::resolve_reference(const String& fragment, const String& origin_uri) const {
    return String(find_by_suffix(to_lower(fragment.utf8().get_data()),
                                 origin_uri.utf8().get_data()).c_str());
}

Dictionary VFSManager::get_game_search_order() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    Dictionary out;

    // Keyed by directory, because that is the identity; the readable name comes alongside,
    // since two entries here can legitimately share one.
    for (const auto& [game, fallbacks] : m_game_fallbacks) {
        PackedStringArray chain;
        for (const auto& f : fallbacks) {
            // A fallback need not be a game in its own right: garrysmod draws on a
            // "sourceengine" folder that ships no manifest, so its own name is all there is.
            const auto named = m_game_names.find(f);
            const std::string label = named != m_game_names.end()
                                          ? named->second
                                          : f.substr(f.find_last_of('/') + 1);
            chain.push_back(String(label.c_str()));
        }
        Dictionary entry;
        const auto named = m_game_names.find(game);
        entry["name"] = String((named != m_game_names.end() ? named->second : game).c_str());
        // The folder too, because a title is not always unique: Episode One and Episode Two
        // both call themselves HALF-LIFE 2, and what tells them apart is where they live.
        entry["folder"] = String(game.substr(game.find_last_of('/') + 1).c_str());
        entry["falls_back_to"] = chain;
        out[String(game.c_str())] = entry;
    }
    return out;
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
        if (!mount.occupied) continue; // never used, or freed by unmount()

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
        // Lets a caller re-mount correctly after a restart: a directory needs
        // mount_directory(), an archive needs mount_container().
        info["is_directory"] = mount.is_directory;
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
