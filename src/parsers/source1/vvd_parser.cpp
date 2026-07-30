#include "vvd_parser.h"

#include "../../core/io/byte_reader.h"

#include <algorithm>
#include <cstring>
#include <optional>

namespace quebratsk::parsers::source1 {

using io::ByteReader;

std::optional<int32_t> VVDParser::read_checksum(std::span<const std::byte> vvd_bytes) {
    const ByteReader reader(vvd_bytes);
    const auto header = reader.array_at<VVDHeader>(0, 1);
    if (header.empty() || header[0].id != kVvdMagic) return std::nullopt;
    return header[0].checksum;
}

std::expected<std::vector<VVDVertex>, VVDParseError> VVDParser::load_vertices(
    std::span<const std::byte> vvd_bytes,
    int32_t lod
) {
    const ByteReader reader(vvd_bytes);

    const auto header_span = reader.array_at<VVDHeader>(0, 1);
    if (header_span.empty()) {
        return std::unexpected(VVDParseError::InvalidHeader);
    }
    const VVDHeader& header = header_span[0];

    if (header.id != kVvdMagic) {
        return std::unexpected(VVDParseError::InvalidHeader);
    }
    if (header.version != kVvdVersion) {
        return std::unexpected(VVDParseError::VersionMismatch);
    }
    if (lod < 0 || lod >= kVvdMaxLods || lod >= header.num_lods) {
        return std::unexpected(VVDParseError::CorruptedData);
    }

    const int32_t wanted = header.num_lod_vertexes[lod];
    if (wanted < 0) {
        return std::unexpected(VVDParseError::CorruptedData);
    }
    if (wanted == 0) {
        return std::vector<VVDVertex>{};
    }

    // The raw array holds LOD 0's count; finer LODs are subsets selected by the fixups.
    const int32_t raw_count = header.num_lod_vertexes[0];
    if (header.vertex_data_start < 0 || raw_count < 0) {
        return std::unexpected(VVDParseError::CorruptedData);
    }

    const auto raw = reader.array_at<VVDVertex>(static_cast<size_t>(header.vertex_data_start),
                                                static_cast<size_t>(raw_count));
    if (raw.empty() && raw_count != 0) {
        return std::unexpected(VVDParseError::CorruptedData);
    }

    std::vector<VVDVertex> out;
    out.reserve(static_cast<size_t>(wanted));

    if (header.num_fixups <= 0) {
        // No fixup table: the array is already in mesh order.
        const size_t take = std::min(static_cast<size_t>(wanted), raw.size());
        out.assign(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(take));
        return out;
    }

    if (header.fixup_table_start < 0) {
        return std::unexpected(VVDParseError::CorruptedData);
    }
    const auto fixups = reader.array_at<VVDFixup>(static_cast<size_t>(header.fixup_table_start),
                                                  static_cast<size_t>(header.num_fixups));
    if (fixups.empty()) {
        return std::unexpected(VVDParseError::CorruptedData);
    }

    // A fixup run belongs to its own LOD and to every finer one, so for the requested
    // LOD we take every run whose `lod` is at least as coarse.
    for (const VVDFixup& fix : fixups) {
        if (fix.lod < lod) continue;

        if (fix.source_vertex_id < 0 || fix.num_vertexes < 0) {
            return std::unexpected(VVDParseError::CorruptedData);
        }
        const size_t src = static_cast<size_t>(fix.source_vertex_id);
        const size_t run = static_cast<size_t>(fix.num_vertexes);
        if (src > raw.size() || run > raw.size() - src) {
            return std::unexpected(VVDParseError::CorruptedData);
        }

        const auto slice = raw.subspan(src, run);
        out.insert(out.end(), slice.begin(), slice.end());
    }

    return out;
}

} // namespace quebratsk::parsers::source1
