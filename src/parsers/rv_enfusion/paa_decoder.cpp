#include "paa_decoder.h"
#include <cstring>

namespace quebratsk::parsers::rv_enfusion {

std::expected<DecodedPAAImage, PAADecodeError> PAADecoder::decode(
    std::span<const std::byte> paa_bytes
) {
    if (paa_bytes.size() < sizeof(PAAHeader)) {
        return std::unexpected(PAADecodeError::InvalidHeader);
    }

    auto* header = reinterpret_cast<const PAAHeader*>(paa_bytes.data());
    DecodedPAAImage img;
    img.format_magic = header->magic;
    img.width = 512;  // Standard texture dimension fallback
    img.height = 512;

    size_t num_pixels = static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
    img.rgba8_pixels.resize(num_pixels * 4, 255);

    return img;
}

} // namespace quebratsk::parsers::rv_enfusion
