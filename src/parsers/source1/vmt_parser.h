#pragma once

#include "../../core/ir/ir_material_data.h"

#include <cstddef>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace quebratsk::parsers::source1 {

enum class VMTParseError {
    EmptyFile,
    InvalidSyntax,
};

class VMTParser {
public:
    /// Reads the material a patch includes, by the path written in it ("materials/x.vmt",
    /// often with backslashes or in another case). Empty when there is no such file.
    using IncludeReader = std::function<std::vector<std::byte>(std::string_view path)>;

    /// Parse VMT KeyValues text into Intermediate Representation material parameters.
    /// A patch material is read without its base: only its own insert pairs count.
    [[nodiscard]] static std::expected<ir::IRMaterialData, VMTParseError> parse(
        std::span<const std::byte> vmt_bytes
    );

    /// The same, following a patch to the material it includes, so the result carries
    /// the base's shader and textures with the patch's changes on top.
    [[nodiscard]] static std::expected<ir::IRMaterialData, VMTParseError> parse(
        std::span<const std::byte> vmt_bytes, const IncludeReader& read_include
    );
};

} // namespace quebratsk::parsers::source1
