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

    ParsedP3DModel result;
    result.mesh_data.source_engine = ir::SourceEngine::RealVirtuality;
    result.mesh_data.name = "BohemiaModel";

    result.skeleton_data.source_engine = ir::SourceEngine::RealVirtuality;
    result.skeleton_data.name = "BohemiaModel_Skeleton";

    return result;
}

} // namespace quebratsk::parsers::rv_enfusion
