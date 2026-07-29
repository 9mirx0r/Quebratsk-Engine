#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>
#include <cstring>

namespace quebratsk::parsers::source1 {

inline constexpr std::array<char, 4> kGmaMagic = {'G', 'M', 'A', 'D'};

#pragma pack(push, 1)
/// Fixed header at the start of a Garry's Mod Addon (.gma) file (22 bytes)
struct GMAHeader {
    char magic[4];          // "GMAD"
    uint8_t version;        // Format version (usually 3)
    uint64_t steam_id;      // Author SteamID64
    uint64_t timestamp;     // Unix timestamp
    uint8_t required_content; // Required content flag
};
#pragma pack(pop)

static_assert(sizeof(GMAHeader) == 22, "GMAHeader size mismatch");

[[nodiscard]] inline bool validate_gma_header(std::span<const std::byte> data) {
    if (data.size() < sizeof(GMAHeader)) return false;
    auto* header = reinterpret_cast<const GMAHeader*>(data.data());
    return std::memcmp(header->magic, kGmaMagic.data(), 4) == 0;
}

} // namespace quebratsk::parsers::source1
