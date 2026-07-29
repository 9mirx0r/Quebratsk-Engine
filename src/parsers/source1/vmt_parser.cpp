#include "vmt_parser.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace quebratsk::parsers::source1 {

static std::string to_lower(std::string_view str) {
    std::string result(str);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::expected<ir::IRMaterialData, VMTParseError> VMTParser::parse(
    std::span<const std::byte> vmt_bytes
) {
    if (vmt_bytes.empty()) {
        return std::unexpected(VMTParseError::EmptyFile);
    }

    std::string text(reinterpret_cast<const char*>(vmt_bytes.data()), vmt_bytes.size());
    std::istringstream stream(text);
    std::string line;

    ir::IRMaterialData mat;
    mat.source_engine = ir::SourceEngine::Source1;

    while (std::getline(stream, line)) {
        // Strip quotes and whitespace
        line.erase(std::remove(line.begin(), line.end(), '"'), line.end());
        std::string lower = to_lower(line);

        if (lower.contains("$basetexture")) {
            size_t pos = lower.find("$basetexture");
            std::string path = line.substr(pos + 12);
            path.erase(0, path.find_first_not_of(" \t"));
            mat.albedo_texture = ir::IRTextureRef{path};
        } else if (lower.contains("$bumpmap")) {
            size_t pos = lower.find("$bumpmap");
            std::string path = line.substr(pos + 8);
            path.erase(0, path.find_first_not_of(" \t"));
            mat.normal_texture = ir::IRTextureRef{path};
            mat.has_compressed_normal = true; // DXT5nm flag
        } else if (lower.contains("$translucent") && lower.contains("1")) {
            mat.is_transparent = true;
        } else if (lower.contains("$nocull") && lower.contains("1")) {
            mat.is_two_sided = true;
        }
    }

    return mat;
}

} // namespace quebratsk::parsers::source1
