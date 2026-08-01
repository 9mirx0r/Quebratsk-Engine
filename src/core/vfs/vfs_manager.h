#pragma once

#include "game_manifest.h"
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
#include <utility>
#include <vector>

namespace quebratsk::vfs {

enum class CompressionType : uint32_t {
    None = 0,
    LZSS_Bohemia = 1,  // PBO "Cprs"
    Deflate = 2,
};

/// Indexed entry: either a slice of a mounted container, or a loose file on disk.
///
/// Deliberately without its own URI field. It used to carry one, which was a character-for-
/// character copy of the key it is stored under: 60,584 redundant string allocations for a
/// two-game setup, rebuilt on every mount. Its one reader, unmount(), has the key in hand.
struct VFSEntry {
    size_t container_index = 0;   // Index into m_containers
    size_t offset = 0;            // Byte offset in container
    size_t disk_size = 0;         // Stored size on disk
    size_t uncompressed_size = 0; // Final size
    CompressionType compression = CompressionType::None;

    /// Which game this one file belongs to, as a slot in the manager's game table, or 0 to
    /// take the container's answer.
    ///
    /// A mounted archive sits inside exactly one game, so its files inherit and this stays 0.
    /// A mounted directory need not: the four GoldSrc games on a machine share one folder,
    /// with valve, cstrike, czero and czeror inside it, so a single mount spans all four.
    /// Attributing those by container put fourteen thousand files in no game at all, and
    /// since GoldSrc keeps nearly everything as loose files, that was most of GoldSrc.
    uint16_t game = 0;

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

    /// Whether this slot is live.
    ///
    /// Occupancy used to be inferred from mapped_file.is_valid(), which silently assumed
    /// every mount is backed by an archive. A mount_directory() mount is not: it has no
    /// mapped file, so its slot read as free and the next mount_container() would recycle
    /// it out from under the live directory — and its entries, left at the default
    /// container_index of 0, were attributed to whatever sat in slot 0.
    bool occupied = false;

    /// A tree of loose files rather than an archive. Nothing to memory-map or unmap.
    bool is_directory = false;

    /// The game directory this archive belongs to, as an absolute lowercase path. Empty
    /// when the file sits outside any game, such as a map bundle in Downloads.
    ///
    /// This is what lets a lookup answer with the file the asking asset was built against
    /// rather than with a same-named file from whichever other game is also mounted. It is
    /// a path and not a name because two different games can be called the same thing.
    std::string game_id;

    /// The game folder's own name, for anything a person reads.
    std::string game_name;

    /// When this mount happened, counting from zero. Two archives of equal standing are
    /// separated by this, so a lookup that could go either way always goes the same way.
    size_t order = 0;
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

    /// Search the index without copying it.
    ///
    /// list_files() returns every indexed URI: a Half-Life 2 plus Garry's Mod setup is
    /// 60,584 strings, several megabytes allocated and thrown away for a caller that only
    /// ever shows a few hundred. This does the filtering where the data already is and
    /// returns only what will be displayed. Measured over that index, filtering to the
    /// same 102 results takes 8.8 ms here against 55.5 ms for list_files() plus the
    /// equivalent GDScript loop.
    ///
    /// `needle` matches anywhere in the URI, case-insensitively; empty matches all.
    /// `extensions` are lowercase and without a dot ("mdl"); empty matches all.
    /// `exclude` is subtracted from the result, and must be applied here rather than by
    ///     the caller: filtering after the limit both truncates the page and inflates the
    ///     total, because the caller only ever sees the part it was given.
    /// `prefixes` restricts the search to those mounts; empty searches all of them. Same
    ///     reasoning as `exclude`: with several games mounted, a caller that wanted one
    ///     game's models and filtered a page afterwards would be handed a page drawn from
    ///     every game and be left with whatever survived.
    /// `limit` caps the returned array, but never the count.
    ///
    /// Returns { "files": PackedStringArray, "total": int }, where `total` is how many
    /// matched before the limit — a UI needs that to say "showing the first 400 of 18,090".
    /// `games` restricts the search to those game directories, as get_game_of() reports
    ///     them; empty searches all of them. Distinct from `prefixes` because the two do not
    ///     line up: one game is several mounts, and one mount can hold four games, which is
    ///     exactly how Steam lays out Half-Life and its three siblings.
    godot::Dictionary find_files(const godot::String& needle,
                                 const godot::PackedStringArray& extensions,
                                 const godot::PackedStringArray& exclude,
                                 const godot::PackedStringArray& prefixes,
                                 int64_t limit,
                                 const godot::PackedStringArray& games
                                     = godot::PackedStringArray()) const;

    /// Read file content as PackedByteArray (decompresses automatically if needed)
    godot::PackedByteArray read_file(const godot::String& vfs_uri) const;

    /// Get uncompressed file size in bytes (-1 if not found)
    int64_t get_file_size(const godot::String& vfs_uri) const;

    /// Get structured metadata for all active mounts (prefix, real_path, engine, file_count)
    godot::Array get_mounts_info() const;

    /// Scan a game directory and report existing archives and importable asset counts
    godot::Dictionary scan_game_directory(const godot::String& real_dir) const;

    /// Get raw zero-copy span for uncompressed entries.
    /// WARNING: the span is only valid until the owning container is unmounted. Prefer
    /// read_owned() unless the caller provably outlives no mount change.
    [[nodiscard]] std::optional<std::span<const std::byte>> get_raw_span(const std::string& vfs_uri_str) const;

    /// Copy an entry's bytes into an owned buffer, transparently decompressing.
    /// Empty result means "missing or unreadable". This is the safe counterpart to
    /// get_raw_span() and the single place that knows how to handle both cases.
    [[nodiscard]] std::vector<std::byte> read_owned(const std::string& vfs_uri_str) const;

    /// The best indexed URI ending with `lowercase_suffix`, or an empty string.
    ///
    /// Legacy material references are path fragments without a mount prefix
    /// ("metal/metalwall001a"), so they can only be resolved by suffix search. With several
    /// games mounted, many files answer to the same fragment, and which one is right depends
    /// entirely on who is asking.
    ///
    /// `origin_uri` is the asset doing the asking. Given one, the search runs in the order
    /// its game declares: its own archive first, then its game, then the games that game
    /// falls back to, then everything else. Without one, the order is still fixed, by when
    /// each archive was mounted. Neither is what this used to do, which was to return
    /// whichever entry the hash table reached first.
    [[nodiscard]] std::string find_by_suffix(const std::string& lowercase_suffix,
                                             const std::string& origin_uri = {}) const;

    /// The game folder a mounted asset belongs to, lowercase, or an empty string.
    [[nodiscard]] std::string game_of(const std::string& vfs_uri) const;

    /// GDScript face of game_of().
    godot::String get_game_of(const godot::String& vfs_uri) const;

    /// GDScript face of find_by_suffix(): resolve a bare reference on behalf of an asset.
    ///
    /// Exposed because it is the one lookup whose answer depends on who is asking, which
    /// makes it the one worth being able to inspect from outside.
    godot::String resolve_reference(const godot::String& fragment,
                                    const godot::String& origin_uri = godot::String()) const;

    /// What each mounted game falls back to, as { game_id: PackedStringArray }.
    godot::Dictionary get_game_search_order() const;

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

    /// Work out which game a container belongs to and remember what that game searches.
    /// Called for every mount, including the side archives a VPK brings with it.
    void adopt_game(MountedContainer& container);

    /// The games a lookup should consult, best first, on behalf of `entry`. Requires m_mutex.
    [[nodiscard]] std::vector<std::string> search_order(const VFSEntry& entry) const;

    /// The game directory an entry belongs to: its own when it has one, its container's
    /// otherwise. Requires m_mutex.
    [[nodiscard]] const std::string& game_id_of(const VFSEntry& entry) const;

    /// The slot a game directory occupies in the table, adding it if new. Requires m_mutex.
    [[nodiscard]] uint16_t intern_game(const std::string& id);

    std::vector<MountedContainer> m_containers;
    std::unordered_map<std::string, VFSEntry> m_index;

    /// What each game falls back to, read from its own manifest, in the order it declares.
    std::unordered_map<std::string, std::vector<std::string>> m_game_fallbacks;

    /// Which game owns a given directory, as { directory, display name }. Reading a
    /// manifest touches the disk and a VPK mounts a dozen side archives out of one folder,
    /// so the answer is kept.
    std::unordered_map<std::string, std::pair<std::string, std::string>> m_game_of_dir;

    /// What each game directory calls itself, for anything a person looks at.
    std::unordered_map<std::string, std::string> m_game_names;

    /// Game directories by slot, so an entry can name one in two bytes. Slot 0 is empty and
    /// means "ask the container".
    std::vector<std::string> m_game_ids{std::string()};
    std::unordered_map<std::string, uint16_t> m_game_slots;

    size_t m_mount_counter = 0;
    mutable std::mutex m_mutex;
};

} // namespace quebratsk::vfs
