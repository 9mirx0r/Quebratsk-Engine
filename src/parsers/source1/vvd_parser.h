#pragma once

#include "structs/vvd_structs.h"

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <vector>

namespace quebratsk::parsers::source1 {

enum class VVDParseError {
    InvalidHeader,
    VersionMismatch,
    CorruptedData,
};

class VVDParser {
public:
    /// Load a .vvd's vertex array for the requested LOD, applying the fixup table.
    ///
    /// When the file carries fixups the on-disk vertex array is not in mesh order: it
    /// has to be rebuilt by copying the runs the table describes. Reading it linearly
    /// yields a scrambled mesh, which is the classic failure with this format.
    ///
    /// Pure data in, pure data out — no Godot types, safe on any thread.
    [[nodiscard]] static std::expected<std::vector<VVDVertex>, VVDParseError> load_vertices(
        std::span<const std::byte> vvd_bytes,
        int32_t lod = 0
    );

    /// Checksum from the header, for cross-checking against the .mdl and .vtx.
    /// Returns nullopt when the header is not a valid VVD.
    [[nodiscard]] static std::optional<int32_t> read_checksum(std::span<const std::byte> vvd_bytes);
};

} // namespace quebratsk::parsers::source1
