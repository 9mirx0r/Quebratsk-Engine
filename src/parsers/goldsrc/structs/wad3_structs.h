#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>
#include <cstring>

namespace quebratsk::parsers::goldsrc {

inline constexpr std::array<char, 4> kWad3Magic = {'W', 'A', 'D', '3'};

#pragma pack(push, 1)
/// Header of a GoldSrc WAD3 texture container (12 bytes)
struct WAD3Header {
    char magic[4];          // "WAD3"
    int32_t num_lumps;      // Number of directory entries
    int32_t dir_offset;     // Offset to directory table
};

/// Directory entry in WAD3 file (32 bytes)
struct WAD3Lump {
    int32_t file_pos;        // Offset in WAD file
    int32_t disk_size;       // Size on disk
    int32_t uncompressed_size; // Size uncompressed
    uint8_t type;            // 0x43 = MIPTEX (Texture), 0x42 = QPIC
    uint8_t compression;     // 0 = Uncompressed
    uint16_t padding;
    char name[16];           // Null-terminated texture name
};

/// Header of a Miptex texture lump within WAD3
struct MiptexHeader {
    char name[16];
    uint32_t width;          // Must be multiple of 16
    uint32_t height;         // Must be multiple of 16
    uint32_t offsets[4];     // Offsets to mipmap levels 0 (1:1), 1 (1:2), 2 (1:4), 3 (1:8)
};
#pragma pack(pop)

static_assert(sizeof(WAD3Header) == 12, "WAD3Header size mismatch");
static_assert(sizeof(WAD3Lump) == 32, "WAD3Lump size mismatch");

[[nodiscard]] inline bool validate_wad3_header(std::span<const std::byte> data) {
    if (data.size() < sizeof(WAD3Header)) return false;
    auto* header = reinterpret_cast<const WAD3Header*>(data.data());
    return std::memcmp(header->magic, kWad3Magic.data(), 4) == 0;
}

} // namespace quebratsk::parsers::goldsrc
