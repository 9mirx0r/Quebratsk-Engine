#include "vmt_parser.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>

namespace quebratsk::parsers::source1 {

namespace {

std::string to_lower(std::string_view str) {
    std::string result(str);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

void trim(std::string& s) {
    const size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        s.clear();
        return;
    }
    const size_t last = s.find_last_not_of(" \t\r\n");
    s = s.substr(first, last - first + 1);
}

/// True when `key` occurs in `lower_line` as a complete identifier rather than as a
/// prefix of a longer one. Without this, "$basetexture" also matched
/// "$basetexturetransform" and "$basetexture2", and the transform's arguments were
/// stored as if they were a texture path.
bool match_key(std::string_view lower_line, std::string_view key,
               const std::string& raw_line, std::string& out_value) {
    const size_t pos = lower_line.find(key);
    if (pos == std::string_view::npos) return false;

    const size_t after = pos + key.size();
    if (after < lower_line.size()) {
        const unsigned char next = static_cast<unsigned char>(lower_line[after]);
        if (std::isalnum(next) || next == '_') return false; // part of a longer key
    }

    out_value = raw_line.substr(after);
    trim(out_value);
    return !out_value.empty();
}

/// VMT booleans are "1"/"0". The previous code tested `line.contains("1")`, which
/// matched a "1" anywhere on the line including inside the key or a path.
bool is_truthy(const std::string& value) {
    return !value.empty() && value[0] == '1';
}

} // namespace

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

    int brace_depth = 0;
    bool shader_captured = false;

    while (std::getline(stream, line)) {
        // Strip end-of-line comments before anything else.
        if (const size_t comment = line.find("//"); comment != std::string::npos) {
            line.erase(comment);
        }
        line.erase(std::remove(line.begin(), line.end(), '"'), line.end());

        std::string trimmed = line;
        trim(trimmed);
        if (trimmed.empty()) continue;

        const int opens = static_cast<int>(std::count(trimmed.begin(), trimmed.end(), '{'));
        const int closes = static_cast<int>(std::count(trimmed.begin(), trimmed.end(), '}'));
        const std::string lower = to_lower(trimmed);

        // The first token before the opening brace is the shader name
        // (VertexLitGeneric, LightmappedGeneric, UnlitGeneric, Water, ...). It was
        // never captured before, so IRMaterialData::shader_name stayed empty.
        if (!shader_captured && brace_depth == 0 && trimmed[0] != '{' && trimmed[0] != '}') {
            std::string shader = trimmed.substr(0, trimmed.find('{'));
            trim(shader);
            if (!shader.empty()) {
                mat.shader_name = shader;
                shader_captured = true;
            }
        }

        // Track nesting so keys inside Proxies{} and other sub-blocks are not mistaken
        // for top-level material parameters.
        if (brace_depth <= 1) {
            std::string value;
            if (match_key(lower, "$basetexture", trimmed, value)) {
                mat.albedo_texture = ir::IRTextureRef{value};
            } else if (match_key(lower, "$bumpmap", trimmed, value) ||
                       match_key(lower, "$normalmap", trimmed, value)) {
                mat.normal_texture = ir::IRTextureRef{value};
            } else if (match_key(lower, "$envmapmask", trimmed, value)) {
                mat.specular_texture = ir::IRTextureRef{value};
            } else if (match_key(lower, "$selfillummask", trimmed, value)) {
                mat.emission_texture = ir::IRTextureRef{value};
            } else if (match_key(lower, "$detail", trimmed, value)) {
                mat.detail_texture = ir::IRTextureRef{value};
            } else if (match_key(lower, "$translucent", trimmed, value)) {
                mat.is_transparent = is_truthy(value);
            } else if (match_key(lower, "$alphatest", trimmed, value)) {
                if (is_truthy(value)) mat.is_transparent = true;
            } else if (match_key(lower, "$nocull", trimmed, value)) {
                mat.is_two_sided = is_truthy(value);
            } else if (match_key(lower, "$additive", trimmed, value)) {
                mat.is_additive = is_truthy(value);
            }
        }

        brace_depth += opens - closes;
        if (brace_depth < 0) brace_depth = 0;
    }

    return mat;
}

} // namespace quebratsk::parsers::source1
