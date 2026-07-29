#pragma once

#include "structs/vtf_structs.h"

#include <expected>
#include <span>
#include <vector>

namespace quebratsk::parsers::source1 {

enum class VTFParseError {
    InvalidHeader,
    VersionMismatch,
    UnsupportedFormat,
    CorruptedData,
};

struct DecodedVTFImage {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = 0;
    std::vector<uint8_t> rgba8_pixels;
};

class VTFParser {
public:
    /// Parse VTF file bytes into high-res RGBA8 pixel buffer
    [[nodiscard]] static std::expected<DecodedVTFImage, VTFParseError> parse(
        std::span<const std::byte> vtf_bytes
    );
};

} // namespace quebratsk::parsers::source1
