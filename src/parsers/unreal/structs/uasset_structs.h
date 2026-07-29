#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <span>

namespace quebratsk::parsers::unreal {

inline constexpr uint32_t kUassetPackageTag = 0x9E2A83C1;

#pragma pack(push, 1)
/// Unreal Engine Package Summary (Header)
struct PackageSummary {
    uint32_t tag;                  // 0x9E2A83C1
    int32_t legacy_file_version;   // -7
    int32_t legacy_ue3_version;
    int32_t file_version_ue4;
    int32_t file_version_licensee;
    int32_t total_header_size;
    uint32_t folder_name_len;
};
#pragma pack(pop)

static_assert(sizeof(PackageSummary) == 28, "PackageSummary size mismatch");

} // namespace quebratsk::parsers::unreal
