#include "vmt_parser.h"
#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>
#include <vector>

namespace quebratsk::parsers::source1 {

namespace {

std::string to_lower(std::string_view str) {
    std::string result(str);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

/// One piece of KeyValues text: a word (quoted or not) or a brace.
struct Token {
    enum Kind { Word, Open, Close } kind;
    std::string text;
};

/// Split KeyValues text into words and braces.
///
/// The text used to be read a line at a time, which took everything after a key as its
/// value: a material written on one line, as tools often write the ones they pack into
/// maps, gave "$basetexture" the value `sprites/glow $additive 1 }`. Reading words
/// instead of lines makes the layout of the text not matter.
///
/// `//` starts a comment to the end of the line, outside quotes. A word in square
/// brackets (`[$X360]`, `[!$OSX]`) is a platform condition on the pair before it and is
/// dropped: the pair holds on the PC the conditions are written against.
std::vector<Token> tokenize(std::string_view text) {
    std::vector<Token> out;
    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        const char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
        } else if (c == '/' && i + 1 < n && text[i + 1] == '/') {
            while (i < n && text[i] != '\n') ++i;
        } else if (c == '{') {
            out.push_back({Token::Open, {}});
            ++i;
        } else if (c == '}') {
            out.push_back({Token::Close, {}});
            ++i;
        } else if (c == '"') {
            const size_t start = ++i;
            while (i < n && text[i] != '"' && text[i] != '\n') ++i;
            out.push_back({Token::Word, std::string(text.substr(start, i - start))});
            if (i < n && text[i] == '"') ++i;
        } else if (c == '[') {
            while (i < n && text[i] != ']' && text[i] != '\n') ++i;
            if (i < n && text[i] == ']') ++i;
        } else {
            const size_t start = i;
            while (i < n && !std::isspace(static_cast<unsigned char>(text[i])) &&
                   text[i] != '{' && text[i] != '}' && text[i] != '"') {
                ++i;
            }
            out.push_back({Token::Word, std::string(text.substr(start, i - start))});
        }
    }
    return out;
}

/// VMT booleans are "1"/"0". The previous code tested `line.contains("1")`, which
/// matched a "1" anywhere on the line including inside the key or a path.
bool is_truthy(const std::string& value) {
    return !value.empty() && value[0] == '1';
}

/// Store one top-level parameter. Keys compare whole, so "$basetexture" is not
/// "$basetexturetransform" nor "$basetexture2".
void apply(ir::IRMaterialData& mat, const std::string& key, const std::string& value) {
    const std::string k = to_lower(key);
    if (k == "$basetexture") {
        if (!value.empty()) mat.albedo_texture = ir::IRTextureRef{value};
    } else if (k == "$bumpmap" || k == "$normalmap") {
        if (!value.empty()) mat.normal_texture = ir::IRTextureRef{value};
    } else if (k == "$envmapmask") {
        if (!value.empty()) mat.specular_texture = ir::IRTextureRef{value};
    } else if (k == "$selfillummask") {
        if (!value.empty()) mat.emission_texture = ir::IRTextureRef{value};
    } else if (k == "$detail") {
        if (!value.empty()) mat.detail_texture = ir::IRTextureRef{value};
    } else if (k == "$translucent") {
        mat.is_transparent = is_truthy(value);
    } else if (k == "$alphatest") {
        if (is_truthy(value)) mat.is_transparent = true;
    } else if (k == "$nocull") {
        mat.is_two_sided = is_truthy(value);
    } else if (k == "$additive") {
        mat.is_additive = is_truthy(value);
    }
}

using PairList = std::vector<std::pair<std::string, std::string>>;

/// A material's pairs, in order, keys lowercased, plus what a patch changes.
struct Pairs {
    std::string shader;
    PairList own;     // depth 1
    PairList insert;  // a patch's insert { }
    PairList replace; // a patch's replace { }
};

Pairs collect(const std::vector<Token>& tokens) {
    Pairs out;
    // The first word is the shader (VertexLitGeneric, LightmappedGeneric, UnlitGeneric,
    // Water, patch, ...), then its block. Inside the block, depth 1 is the material's own
    // pairs; a word followed by a brace opens a sub-block (Proxies, a fallback shader,
    // a patch's insert) whose pairs are not the material's own.
    size_t i = 0;
    if (i < tokens.size() && tokens[i].kind == Token::Word) {
        out.shader = tokens[i].text;
        ++i;
    }

    std::vector<std::string> blocks; // the name of each open block, outermost first
    std::string pending;             // a word that turned out to name the next block
    while (i < tokens.size()) {
        const Token& t = tokens[i];
        if (t.kind == Token::Open) {
            blocks.push_back(to_lower(pending));
            pending.clear();
            ++i;
        } else if (t.kind == Token::Close) {
            if (!blocks.empty()) blocks.pop_back();
            pending.clear();
            ++i;
        } else if (i + 1 < tokens.size() && tokens[i + 1].kind == Token::Word) {
            auto pair = std::make_pair(to_lower(t.text), tokens[i + 1].text);
            if (blocks.size() == 1) {
                out.own.push_back(std::move(pair));
            } else if (blocks.size() == 2 && blocks[1] == "insert") {
                out.insert.push_back(std::move(pair));
            } else if (blocks.size() == 2 && blocks[1] == "replace") {
                out.replace.push_back(std::move(pair));
            }
            pending.clear();
            i += 2;
        } else {
            // A word before a brace names a sub-block; a word at the very end has no
            // value.
            pending = t.text;
            ++i;
        }
    }
    return out;
}

PairList::iterator find_key(PairList& list, const std::string& key) {
    return std::find_if(list.begin(), list.end(),
                        [&](const auto& p) { return p.first == key; });
}

/// The shader and pairs a material ends up with once every patch has been followed.
///
/// A patch (Valve Developer Community, "Patch") takes every pair of the material its
/// `include` names; its `insert` pairs are then added, overriding any already there, and
/// an empty value removes one; its `replace` pairs change pairs the base already has.
/// The shader stays the base's: a patch cannot change it.
///
/// Maps compiled with cube maps are full of these. The map compiler writes one patch per
/// brush face near an env_cubemap, naming the face's real material and replacing only
/// $envmap, so read alone they name no texture at all.
void resolve(const Pairs& m, const VMTParser::IncludeReader* read_include, int depth,
             std::string& shader, PairList& pairs) {
    shader = m.shader;
    if (to_lower(m.shader) != "patch") {
        pairs = m.own;
        return;
    }

    pairs.clear();
    std::string include;
    for (const auto& [k, v] : m.own) {
        if (k == "include") include = v;
    }
    // Some later games let a patch include another patch, so a chain is followed, but
    // not round and round.
    if (!include.empty() && read_include && *read_include && depth < 8) {
        const std::vector<std::byte> base_bytes = (*read_include)(include);
        if (!base_bytes.empty()) {
            const std::string_view text(reinterpret_cast<const char*>(base_bytes.data()),
                                        base_bytes.size());
            resolve(collect(tokenize(text)), read_include, depth + 1, shader, pairs);
        }
    }

    for (const auto& [k, v] : m.insert) {
        auto it = find_key(pairs, k);
        if (v.empty()) {
            if (it != pairs.end()) pairs.erase(it);
        } else if (it != pairs.end()) {
            it->second = v;
        } else {
            pairs.emplace_back(k, v);
        }
    }
    for (const auto& [k, v] : m.replace) {
        // The wiki only says these replace; a key the base does not have is left out.
        if (auto it = find_key(pairs, k); it != pairs.end()) it->second = v;
    }
}

std::expected<ir::IRMaterialData, VMTParseError> parse_with(
    std::span<const std::byte> vmt_bytes, const VMTParser::IncludeReader* read_include) {
    if (vmt_bytes.empty()) {
        return std::unexpected(VMTParseError::EmptyFile);
    }

    const std::string_view text(reinterpret_cast<const char*>(vmt_bytes.data()),
                                vmt_bytes.size());

    ir::IRMaterialData mat;
    mat.source_engine = ir::SourceEngine::Source1;

    PairList pairs;
    resolve(collect(tokenize(text)), read_include, 0, mat.shader_name, pairs);
    for (const auto& [k, v] : pairs) apply(mat, k, v);
    return mat;
}

} // namespace

std::expected<ir::IRMaterialData, VMTParseError> VMTParser::parse(
    std::span<const std::byte> vmt_bytes
) {
    return parse_with(vmt_bytes, nullptr);
}

std::expected<ir::IRMaterialData, VMTParseError> VMTParser::parse(
    std::span<const std::byte> vmt_bytes, const IncludeReader& read_include
) {
    return parse_with(vmt_bytes, &read_include);
}

} // namespace quebratsk::parsers::source1
