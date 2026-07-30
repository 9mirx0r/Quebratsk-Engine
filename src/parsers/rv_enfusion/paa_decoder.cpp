#include "paa_decoder.h"
#include <cstring>

namespace quebratsk::parsers::rv_enfusion {

std::expected<ir::IRTextureData, PAADecodeError> PAADecoder::decode(
    std::span<const std::byte> paa_bytes
) {
    if (paa_bytes.size() < sizeof(PAAHeader)) {
        return std::unexpected(PAADecodeError::InvalidHeader);
    }

    // NOT IMPLEMENTED. The previous body ignored the file entirely, hardcoded
    // 512x512 and returned a solid white image, so every Arma/DayZ texture silently
    // imported as a blank square that looked like a successful decode.
    //
    // A real implementation must read the PAA mipmap table, then dispatch on the
    // format magic (DXT1/DXT5 via quebratsk::image, or the RGBA/GRAY variants).
    return std::unexpected(PAADecodeError::UnsupportedFormat);
}

} // namespace quebratsk::parsers::rv_enfusion
