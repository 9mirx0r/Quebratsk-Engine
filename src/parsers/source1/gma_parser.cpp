#include "gma_parser.h"
#include <algorithm>
#include <cstring>

namespace quebratsk::parsers::source1 {

std::expected<ParsedGMAContainer, GMAParseError> GMAParser::parse(
    std::span<const std::byte> gma_bytes
) {
    if (!validate_gma_header(gma_bytes)) {
        return std::unexpected(GMAParseError::InvalidHeader);
    }

    auto* header = reinterpret_cast<const GMAHeader*>(gma_bytes.data());
    ParsedGMAContainer container;
    container.steam_id = header->steam_id;

    size_t cursor = sizeof(GMAHeader);

    auto read_string = [&](std::string& out_str) {
        size_t start = cursor;
        while (cursor < gma_bytes.size() && static_cast<char>(gma_bytes[cursor]) != '\0') {
            cursor++;
        }
        if (cursor < gma_bytes.size()) {
            out_str = std::string(reinterpret_cast<const char*>(gma_bytes.data() + start), cursor - start);
            cursor++; // Skip null terminator
        }
    };

    read_string(container.addon_name);
    read_string(container.addon_description);
    read_string(container.author_name);

    if (cursor + 4 > gma_bytes.size()) {
        return std::unexpected(GMAParseError::CorruptedTable);
    }
    cursor += 4; // Addon version (int32)

    // Parse file entries table
    while (cursor + 4 < gma_bytes.size()) {
        uint32_t file_num = 0;
        std::memcpy(&file_num, gma_bytes.data() + cursor, sizeof(uint32_t));
        cursor += 4;

        if (file_num == 0) break; // Terminating file number

        GMAFileRecord rec;
        rec.file_index = file_num;

        read_string(rec.relative_path);
        std::replace(rec.relative_path.begin(), rec.relative_path.end(), '\\', '/');

        if (cursor + 8 + 4 > gma_bytes.size()) {
            return std::unexpected(GMAParseError::CorruptedTable);
        }

        std::memcpy(&rec.file_size, gma_bytes.data() + cursor, sizeof(uint64_t));
        cursor += 8;

        std::memcpy(&rec.crc32, gma_bytes.data() + cursor, sizeof(uint32_t));
        cursor += 4;

        container.files.push_back(rec);
    }

    // Set file data offsets
    size_t blob_cursor = cursor;
    for (auto& file_rec : container.files) {
        file_rec.data_offset = blob_cursor;
        blob_cursor += static_cast<size_t>(file_rec.file_size);
    }

    return container;
}

} // namespace quebratsk::parsers::source1
