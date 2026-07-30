#include "vpk2_parser.h"

#include "../../core/io/byte_reader.h"

#include <cstring>

namespace quebratsk::parsers::source2 {

using io::ByteReader;

std::expected<ParsedVPK2Directory, VPK2ParseError> VPK2Parser::parse_directory(
    std::span<const std::byte> vpk_bytes
) {
    ByteReader reader(vpk_bytes);

    const auto magic = reader.read_at<uint32_t>(0);
    if (!magic || *magic != kVpkMagic) {
        return std::unexpected(VPK2ParseError::InvalidHeader);
    }
    const auto version = reader.read_at<uint32_t>(4);
    const auto tree_size = reader.read_at<uint32_t>(8);
    if (!version || !tree_size) {
        return std::unexpected(VPK2ParseError::InvalidHeader);
    }

    // v1 and v2 differ only in what follows the tree, so the walk itself is identical.
    size_t header_size = 0;
    if (*version == 1) {
        header_size = sizeof(VPK1Header);
    } else if (*version == 2) {
        header_size = sizeof(VPK2Header);
    } else {
        return std::unexpected(VPK2ParseError::UnsupportedVersion);
    }

    ParsedVPK2Directory out;
    out.version = *version;

    if (!io::range_fits(header_size, *tree_size, 1, vpk_bytes.size())) {
        return std::unexpected(VPK2ParseError::CorruptedTree);
    }
    out.tree_end = header_size + *tree_size;

    if (!reader.seek(header_size)) {
        return std::unexpected(VPK2ParseError::CorruptedTree);
    }

    // extension -> path -> filename, each level a run terminated by an empty string.
    while (true) {
        if (reader.position() >= out.tree_end) break;

        const auto ext = reader.read_cstr();
        if (!ext) return std::unexpected(VPK2ParseError::CorruptedTree);
        if (ext->empty()) break; // end of the tree

        while (true) {
            const auto dir = reader.read_cstr();
            if (!dir) return std::unexpected(VPK2ParseError::CorruptedTree);
            if (dir->empty()) break; // end of this extension's paths

            while (true) {
                const auto name = reader.read_cstr();
                if (!name) return std::unexpected(VPK2ParseError::CorruptedTree);
                if (name->empty()) break; // end of this path's files

                const auto rec = reader.read<VPK2DirectoryEntry>();
                if (!rec) return std::unexpected(VPK2ParseError::CorruptedTree);

                // The tree carries no length prefixes, so a desynchronised walk would
                // happily emit garbage entries. Checking the terminator is the only
                // cheap way to notice before the damage spreads.
                if (rec->terminator != kVpkEntryTerminator) {
                    return std::unexpected(VPK2ParseError::CorruptedTree);
                }

                VPK2Entry entry;
                // A lone space means "root" for a path and "no extension" for an ext.
                entry.path.reserve(dir->size() + name->size() + ext->size() + 2);
                if (*dir != " ") {
                    entry.path += *dir;
                    entry.path += '/';
                }
                entry.path += *name;
                if (*ext != " ") {
                    entry.path += '.';
                    entry.path += *ext;
                }

                entry.crc = rec->crc;
                entry.archive_index = rec->archive_index;
                entry.length = rec->entry_length;
                entry.preload_bytes = rec->preload_bytes;

                // Inline data sits past the whole tree; side-archive offsets are already
                // absolute within their own file.
                entry.offset = entry.is_inline()
                             ? out.tree_end + rec->entry_offset
                             : rec->entry_offset;

                if (rec->preload_bytes > 0) {
                    entry.preload_offset = reader.position();
                    if (!reader.skip(rec->preload_bytes)) {
                        return std::unexpected(VPK2ParseError::CorruptedTree);
                    }
                }

                if (!entry.is_inline() &&
                    static_cast<int32_t>(entry.archive_index) > out.max_archive_index) {
                    out.max_archive_index = static_cast<int32_t>(entry.archive_index);
                }

                out.entries.push_back(std::move(entry));
            }
        }
    }

    return out;
}

} // namespace quebratsk::parsers::source2
