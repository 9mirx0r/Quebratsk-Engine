#include "pbo_parser.h"
#include <cstring>

namespace quebratsk::parsers::rv_enfusion {

std::expected<ParsedPBOArchive, PBOParseError> PBOParser::parse(
    std::span<const std::byte> pbo_bytes
) {
    if (pbo_bytes.empty()) {
        return std::unexpected(PBOParseError::InvalidHeader);
    }

    ParsedPBOArchive archive;
    size_t cursor = 0;

    while (cursor < pbo_bytes.size()) {
        size_t name_start = cursor;
        while (cursor < pbo_bytes.size() && static_cast<char>(pbo_bytes[cursor]) != '\0') {
            cursor++;
        }
        if (cursor >= pbo_bytes.size()) break;

        std::string name(reinterpret_cast<const char*>(pbo_bytes.data() + name_start));
        cursor++; // Skip null terminator

        if (cursor + sizeof(PBOEntryFields) > pbo_bytes.size()) break;

        PBOEntryFields fields;
        std::memcpy(&fields, pbo_bytes.data() + cursor, sizeof(fields));
        cursor += sizeof(fields);

        if (name.empty() && fields.packing_method == 0 && fields.original_size == 0) {
            break; // End of directory header
        }

        if (fields.packing_method == kPboMagicVers) {
            cursor += fields.data_size; // Skip version block properties
            continue;
        }

        PBOFileEntry entry;
        entry.filename = name;
        entry.packing_method = fields.packing_method;
        entry.original_size = static_cast<size_t>(fields.original_size);
        entry.data_size = static_cast<size_t>(fields.data_size == 0 ? fields.original_size : fields.data_size);

        archive.files.push_back(entry);
    }

    size_t payload_offset = cursor;
    for (auto& file : archive.files) {
        file.data_offset = payload_offset;
        payload_offset += file.data_size;
    }

    return archive;
}

} // namespace quebratsk::parsers::rv_enfusion
