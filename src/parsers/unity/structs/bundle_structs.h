#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>

namespace quebratsk::parsers::unity {

inline constexpr std::array<char, 7> kUnityRawMagic = {'U', 'n', 'i', 't', 'y', 'R', 'a'};
inline constexpr std::array<char, 7> kUnityFSMagic  = {'U', 'n', 'i', 't', 'y', 'F', 'S'};

#pragma pack(push, 1)
/// Unity AssetBundle Version 6 Header
struct UnityFSHeader {
    char magic[7];          // "UnityFS"
    uint32_t version;       // 6 or 7
    char unity_version[16]; // e.g., "2021.3.16f1"
    char engine_version[16];
    uint64_t file_size;
    uint32_t compressed_blocks_info_size;
    uint32_t uncompressed_blocks_info_size;
    uint32_t flags;
};
#pragma pack(pop)

static_assert(sizeof(UnityFSHeader) == 63, "UnityFSHeader size mismatch");

} // namespace quebratsk::parsers::unity
