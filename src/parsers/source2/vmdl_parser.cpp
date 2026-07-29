#include "vmdl_parser.h"
#include "../../core/math/axis_remap.h"

namespace quebratsk::parsers::source2 {

std::expected<ParsedSource2Model, VMDLParseError> VMDLParser::parse(
    std::span<const std::byte> vmdl_bytes
) {
    if (vmdl_bytes.size() < sizeof(Source2ResourceHeader)) {
        return std::unexpected(VMDLParseError::InvalidHeader);
    }

    ParsedSource2Model result;
    result.mesh_data.source_engine = ir::SourceEngine::Source1; // Source Engine Family
    result.mesh_data.name = "Source2Model";

    result.skeleton_data.source_engine = ir::SourceEngine::Source1;
    result.skeleton_data.name = "Source2Model_Skeleton";

    return result;
}

} // namespace quebratsk::parsers::source2
