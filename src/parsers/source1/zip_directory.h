#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace quebratsk::parsers::source1 {

/// One file listed in a zip.
struct ZipEntry {
    std::string name;            // as written in the zip, "/" separated
    uint16_t method = 0;         // 0 = stored as is; 8 = deflate; 14 = LZMA
    size_t offset = 0;           // where its bytes start, counted from the zip's first byte
    size_t stored_size = 0;
    size_t size = 0;             // once unpacked

    [[nodiscard]] bool is_stored() const { return method == 0 && stored_size == size; }
};

/// The file list of a zip, read from the directory at its end.
///
/// A Source level carries one of these inside itself (lump 40, the "pakfile"): the
/// materials, models and cubemaps made for that level alone. The compiler that packs them
/// stores them as they are, so a stored file's bytes can be read straight out of the level.
/// Entries that are folders are left out. A zip that is cut short or whose directory points
/// outside it gives what could be read before that, or nothing.
[[nodiscard]] std::vector<ZipEntry> read_zip_directory(std::span<const std::byte> zip);

/// Where a Source level keeps its pakfile, or an empty span when it has none.
[[nodiscard]] std::span<const std::byte> vbsp_pakfile(std::span<const std::byte> level);

} // namespace quebratsk::parsers::source1
