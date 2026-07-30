#include "vvd_parser.h"

#include <cstring>
#include <optional>

namespace quebratsk::parsers::source1 {

namespace {

/// Overflow-safe "do `count` elements of `elem_size` fit at `offset`?".
bool fits(size_t offset, size_t count, size_t elem_size, size_t total) {
    if (offset > total) return false;
    if (elem_size == 0) return count == 0;
    return count <= (total - offset) / elem_size;
}

} // namespace

std::optional<int32_t> VVDParser::read_checksum(std::span<const std::byte> vvd_bytes) {
    if (vvd_bytes.size() < sizeof(VVDHeader)) return std::nullopt;
    const auto* header = reinterpret_cast<const VVDHeader*>(vvd_bytes.data());
    if (header->id != kVvdMagic) return std::nullopt;
    return header->checksum;
}

std::expected<std::vector<VVDVertex>, VVDParseError> VVDParser::load_vertices(
    std::span<const std::byte> vvd_bytes,
    int32_t lod
) {
    if (vvd_bytes.size() < sizeof(VVDHeader)) {
        return std::unexpected(VVDParseError::InvalidHeader);
    }

    const auto* header = reinterpret_cast<const VVDHeader*>(vvd_bytes.data());
    if (header->id != kVvdMagic) {
        return std::unexpected(VVDParseError::InvalidHeader);
    }
    if (header->version != kVvdVersion) {
        return std::unexpected(VVDParseError::VersionMismatch);
    }
    if (lod < 0 || lod >= kVvdMaxLods || lod >= header->num_lods) {
        return std::unexpected(VVDParseError::CorruptedData);
    }

    const int32_t wanted = header->num_lod_vertexes[lod];
    if (wanted < 0) {
        return std::unexpected(VVDParseError::CorruptedData);
    }
    if (wanted == 0) {
        return std::vector<VVDVertex>{};
    }

    if (header->vertex_data_start < 0) {
        return std::unexpected(VVDParseError::CorruptedData);
    }
    const size_t vertex_start = static_cast<size_t>(header->vertex_data_start);

    // The raw array holds LOD 0's count; finer LODs are subsets selected by the fixups.
    const int32_t raw_count = header->num_lod_vertexes[0];
    if (raw_count < 0 ||
        !fits(vertex_start, static_cast<size_t>(raw_count), sizeof(VVDVertex), vvd_bytes.size())) {
        return std::unexpected(VVDParseError::CorruptedData);
    }

    const auto* raw = reinterpret_cast<const VVDVertex*>(vvd_bytes.data() + vertex_start);

    std::vector<VVDVertex> out;
    out.reserve(static_cast<size_t>(wanted));

    if (header->num_fixups <= 0) {
        // No fixup table: the array is already in mesh order.
        const size_t take = static_cast<size_t>(wanted) <= static_cast<size_t>(raw_count)
                          ? static_cast<size_t>(wanted)
                          : static_cast<size_t>(raw_count);
        out.assign(raw, raw + take);
        return out;
    }

    if (header->fixup_table_start < 0 ||
        !fits(static_cast<size_t>(header->fixup_table_start),
              static_cast<size_t>(header->num_fixups), sizeof(VVDFixup), vvd_bytes.size())) {
        return std::unexpected(VVDParseError::CorruptedData);
    }

    const auto* fixups = reinterpret_cast<const VVDFixup*>(
        vvd_bytes.data() + header->fixup_table_start);

    // A fixup run belongs to its own LOD and to every finer one, so for the requested
    // LOD we take every run whose `lod` is at least as coarse.
    for (int32_t i = 0; i < header->num_fixups; ++i) {
        const VVDFixup& fix = fixups[i];
        if (fix.lod < lod) continue;

        if (fix.source_vertex_id < 0 || fix.num_vertexes < 0) {
            return std::unexpected(VVDParseError::CorruptedData);
        }
        const size_t src = static_cast<size_t>(fix.source_vertex_id);
        const size_t run = static_cast<size_t>(fix.num_vertexes);
        if (src > static_cast<size_t>(raw_count) ||
            run > static_cast<size_t>(raw_count) - src) {
            return std::unexpected(VVDParseError::CorruptedData);
        }

        out.insert(out.end(), raw + src, raw + src + run);
    }

    return out;
}

} // namespace quebratsk::parsers::source1
