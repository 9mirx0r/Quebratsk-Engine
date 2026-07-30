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

    // Reject implausible grid dimensions before any arithmetic. Without this, a crafted
    // header (e.g. 0x7FFFFFFF x 0x7FFFFFFF) makes num_cells * sizeof(float) wrap around
    // size_t, the bounds check below passes, and assign() walks gigabytes past the mapping.
    // Altis, the largest shipped Arma 3 terrain, is 8192x8192.
    constexpr uint32_t kMaxWrpGridDim = 8192;
    if (header->grid_width == 0 || header->grid_height == 0 ||
        header->grid_width > kMaxWrpGridDim || header->grid_height > kMaxWrpGridDim) {
        return std::unexpected(WRPParseError::InvalidHeader);
    }

    ir::IRTerrainData terrain;
    terrain.source_engine = ir::SourceEngine::RealVirtuality;
    terrain.grid_width = static_cast<int32_t>(header->grid_width);
    terrain.grid_height = static_cast<int32_t>(header->grid_height);
    terrain.cell_size = 10.0f; // Default 10 meters per grid cell (OPRW metric)

    const size_t num_cells = static_cast<size_t>(header->grid_width) * header->grid_height;
    constexpr size_t elevation_offset = sizeof(WRPHeader);

    if (wrp_bytes.size() < elevation_offset) {
        return std::unexpected(WRPParseError::InvalidHeader);
    }
    // Division instead of multiplication: cannot overflow.
    if (num_cells > (wrp_bytes.size() - elevation_offset) / sizeof(float)) {
        return std::unexpected(WRPParseError::CorruptedData);
    }

    const auto payload = wrp_bytes.subspan(elevation_offset, num_cells * sizeof(float));
    terrain.heightmap.resize(num_cells);
    std::memcpy(terrain.heightmap.data(), payload.data(), payload.size_bytes());

    return terrain;
}

} // namespace quebratsk::parsers::rv_enfusion
