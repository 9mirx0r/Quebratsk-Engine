#include "p3d_mlod_parser.h"
#include "../../core/math/axis_remap.h"
#include <cstring>

namespace quebratsk::parsers::rv_enfusion {

std::expected<ParsedP3DModel, P3DParseError> P3DMLODParser::parse(
    std::span<const std::byte> p3d_bytes
) {
    if (p3d_bytes.size() < sizeof(P3DHeader)) {
        return std::unexpected(P3DParseError::InvalidHeader);
    }

    auto* header = reinterpret_cast<const P3DHeader*>(p3d_bytes.data());
    bool is_mlod = (std::memcmp(header->magic, kP3dMlodMagic.data(), 4) == 0);
    bool is_odol = (std::memcmp(header->magic, kP3dOdolMagic.data(), 4) == 0);

    if (!is_mlod && !is_odol) {
        return std::unexpected(P3DParseError::InvalidHeader);
    }

    // NOT IMPLEMENTED. Neither MLOD nor ODOL geometry is read.
    //
    // This used to return a *successful* ParsedP3DModel named "BohemiaModel" carrying
    // zero surfaces and zero bones, so every caller saw a valid parse of an empty model
    // and had no way to tell that nothing had been decoded. It also accepted ODOL, which
    // is a different container entirely. Report the truth instead: the caller can then
    // say so, and the format tables can stop claiming Arma and DayZ models import.
    (void)is_mlod;
    (void)is_odol;
    return std::unexpected(P3DParseError::UnsupportedVersion);
}

} // namespace quebratsk::parsers::rv_enfusion
