#include "xob_parser.h"
#include "../../core/math/axis_remap.h"
#include <cstring>

namespace quebratsk::parsers::enfusion {

std::expected<ParsedXOBModel, XOBParseError> XOBParser::parse(
    std::span<const std::byte> xob_bytes
) {
    if (xob_bytes.size() < sizeof(XOBHeader)) {
        return std::unexpected(XOBParseError::InvalidHeader);
    }

    auto* header = reinterpret_cast<const XOBHeader*>(xob_bytes.data());
    if (std::memcmp(header->magic, kXobMagic.data(), 4) != 0) {
        return std::unexpected(XOBParseError::InvalidHeader);
    }

    ParsedXOBModel result;
    result.mesh_data.source_engine = ir::SourceEngine::RealVirtuality;
    result.mesh_data.name = "EnfusionXOBModel";

    result.skeleton_data.source_engine = ir::SourceEngine::RealVirtuality;
    result.skeleton_data.name = "EnfusionXOBModel_Skeleton";

    return result;
}

} // namespace quebratsk::parsers::enfusion
