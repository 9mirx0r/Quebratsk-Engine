#include "unity_mesh_parser.h"
#include "../../core/math/axis_remap.h"

namespace quebratsk::parsers::unity {

std::expected<ParsedUnityModel, UnityMeshParseError> UnityMeshParser::parse(
    std::span<const std::byte> mesh_bytes
) {
    if (mesh_bytes.empty()) {
        return std::unexpected(UnityMeshParseError::InvalidHeader);
    }

    ParsedUnityModel result;
    result.mesh_data.source_engine = ir::SourceEngine::Source1; // Modern generic 3D engine mapping
    result.mesh_data.name = "UnityMeshModel";

    result.skeleton_data.source_engine = ir::SourceEngine::Source1;
    result.skeleton_data.name = "UnityMeshModel_Skeleton";

    return result;
}

} // namespace quebratsk::parsers::unity
