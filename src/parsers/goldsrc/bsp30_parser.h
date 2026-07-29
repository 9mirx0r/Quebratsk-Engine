#pragma once

#include "../../core/ir/ir_collision_data.h"
#include "../../core/ir/ir_mesh_data.h"
#include "structs/bsp30_structs.h"

#include <expected>
#include <span>
#include <string>

namespace quebratsk::parsers::goldsrc {

enum class BSP30ParseError {
    InvalidHeader,
    VersionMismatch,
    TruncatedLump,
    CorruptedData,
};

struct ParsedBSP30Map {
    ir::IRMeshData mesh_data;
    ir::IRCollisionData collision_data;
    std::string entity_string; // Raw ASCII entities lump
};

class BSP30Parser {
public:
    /// Parse raw BSP v30 file data into Intermediate Representation mesh & collision
    [[nodiscard]] static std::expected<ParsedBSP30Map, BSP30ParseError> parse(
        std::span<const std::byte> bsp_bytes
    );
};

} // namespace quebratsk::parsers::goldsrc
