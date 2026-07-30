#pragma once

#include "structs/paa_structs.h"
#include "../../core/ir/ir_texture_data.h"

#include <expected>
#include <span>
#include <vector>

namespace quebratsk::parsers::rv_enfusion {

enum class PAADecodeError {
    InvalidHeader,
    UnsupportedType,
    UnsupportedFormat,
    CorruptedData,
};

class PAADecoder {
public:
    /// Decode a PAA texture into RGBA8.
    ///
    /// NOT IMPLEMENTED: currently always returns UnsupportedFormat. It previously
    /// returned a hardcoded 512x512 white image regardless of input, which was
    /// indistinguishable from a successful decode.
    [[nodiscard]] static std::expected<ir::IRTextureData, PAADecodeError> decode(
        std::span<const std::byte> paa_bytes
    );
};

} // namespace quebratsk::parsers::rv_enfusion
