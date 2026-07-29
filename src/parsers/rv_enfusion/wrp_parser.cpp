#include "wrp_parser.h"
#include "../../core/math/axis_remap.h"
#include <cstring>

namespace quebratsk::parsers::rv_enfusion {

std::expected<ir::IRTerrainData, WRPParseError> WRPParser::parse(
    std::span<const std::byte> wrp_bytes
) {
    if (wrp_bytes.size() < sizeof(WRPHeader)) {
        return std::unexpected(WRPParseError::InvalidHeader);
    }

    auto* header = reinterpret_cast<const WRPHeader*>(wrp_bytes.data());
    bool is_oprw = (std::memcmp(header->magic, kWrpMagicOPRW.data(), 4) == 0);
    bool is_8wvr = (std::memcmp(header->magic, kWrpMagic8WVR.data(), 4) == 0);

    if (!is_oprw && !is_8wvr) {
        return std::unexpected(WRPParseError::InvalidHeader);
    }

    ir::IRTerrainData terrain;
    terrain.source_engine = ir::SourceEngine::RealVirtuality;
    terrain.grid_width = static_cast<int32_t>(header->grid_width);
    terrain.grid_height = static_cast<int32_t>(header->grid_height);
    terrain.cell_size = 10.0f; // Default 10 meters per grid cell (OPRW metric)

    size_t num_cells = static_cast<size_t>(terrain.grid_width) * static_cast<size_t>(terrain.grid_height);
    size_t elevation_offset = sizeof(WRPHeader);

    if (elevation_offset + num_cells * sizeof(float) <= wrp_bytes.size()) {
        const float* heights = reinterpret_cast<const float*>(wrp_bytes.data() + elevation_offset);
        terrain.heightmap.assign(heights, heights + num_cells);
    }

    return terrain;
}

} // namespace quebratsk::parsers::rv_enfusion
