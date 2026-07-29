#include "uasset_parser.h"
#include "../../core/math/axis_remap.h"

namespace quebratsk::parsers::unreal {

std::expected<ParsedUnrealAsset, UAssetParseError> UAssetParser::parse(
    std::span<const std::byte> uasset_bytes
) {
    if (uasset_bytes.size() < sizeof(PackageSummary)) {
        return std::unexpected(UAssetParseError::InvalidHeader);
    }

    auto* summary = reinterpret_cast<const PackageSummary*>(uasset_bytes.data());
    if (summary->tag != kUassetPackageTag) {
        return std::unexpected(UAssetParseError::InvalidHeader);
    }

    ParsedUnrealAsset result;
    result.mesh_data.source_engine = ir::SourceEngine::Source1; // Modern engine mapping
    result.mesh_data.name = "UnrealAssetModel";

    result.skeleton_data.source_engine = ir::SourceEngine::Source1;
    result.skeleton_data.name = "UnrealAssetModel_Skeleton";

    return result;
}

} // namespace quebratsk::parsers::unreal
