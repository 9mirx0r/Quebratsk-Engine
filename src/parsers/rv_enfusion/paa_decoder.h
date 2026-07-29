#pragma once

#include "structs/paa_structs.h"

#include <expected>
#include <span>
#include <vector>

namespace quebratsk::parsers::rv_enfusion {

enum class PAADecodeError {
    InvalidHeader,
    UnsupportedType,
    CorruptedData,
};

struct DecodedPAAImage {
    uint32_t width = 0;
    uint32_t height = 0;
    uint16_t format_magic = 0;
    std::vector<uint8_t> rgba8_pixels;
};

class PAADecoder {
public:
    /// Decode PAA texture mipmap stream into uncompressed RGBA8 pixels
    [[nodiscard]] static std::expected<DecodedPAAImage, PAADecodeError> decode(
        std::span<const std::byte> paa_bytes
    );
};

} // namespace quebratsk::parsers::rv_enfusion
