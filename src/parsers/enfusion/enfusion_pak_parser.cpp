#include "enfusion_pak_parser.h"
#include <cstring>

namespace quebratsk::parsers::enfusion {

std::expected<std::vector<EnfusionPakEntry>, EnfusionPakParseError> EnfusionPakParser::parse(
    std::span<const std::byte> pak_bytes
) {
    if (pak_bytes.size() < sizeof(EnfusionPakHeader)) {
        return std::unexpected(EnfusionPakParseError::InvalidHeader);
    }

    auto* header = reinterpret_cast<const EnfusionPakHeader*>(pak_bytes.data());
    if (std::memcmp(header->magic, kEnfusionPakMagic.data(), 4) != 0) {
        return std::unexpected(EnfusionPakParseError::InvalidHeader);
    }

    // entry_count is attacker-controlled. Reserving it blindly lets a 12-byte file
    // request a multi-gigabyte allocation; with exceptions disabled (godot-cpp sets
    // _HAS_EXCEPTIONS=0) the resulting length_error/bad_alloc becomes terminate().
    // Bound it by what the file could physically contain.
    const size_t max_possible_entries = pak_bytes.size() / sizeof(EnfusionPakHeader);
    if (header->entry_count > max_possible_entries) {
        return std::unexpected(EnfusionPakParseError::CorruptedData);
    }

    std::vector<EnfusionPakEntry> entries;
    entries.reserve(static_cast<size_t>(header->entry_count));
    return entries;
}

} // namespace quebratsk::parsers::enfusion
