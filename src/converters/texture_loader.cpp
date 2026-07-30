#include "texture_loader.h"
#include "texture_converter.h"

#include "../core/vfs/texture_cache.h"
#include "../parsers/source1/vtf_parser.h"
#include "../parsers/goldsrc/wad3_parser.h"

#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cctype>
#include <vector>

namespace quebratsk::converters {

using namespace godot;

namespace {

std::string normalize_ref(std::string ref) {
    std::replace(ref.begin(), ref.end(), '\\', '/');
    std::transform(ref.begin(), ref.end(), ref.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // VMT paths are relative to the materials root and occasionally start with "./".
    while (ref.starts_with("./")) ref.erase(0, 2);
    while (ref.starts_with("/")) ref.erase(0, 1);

    // Trailing whitespace survives the KeyValues parse on some hand-edited files.
    while (!ref.empty() && (ref.back() == ' ' || ref.back() == '\t' || ref.back() == '\r')) {
        ref.pop_back();
    }
    return ref;
}

bool has_extension(const std::string& s) {
    const size_t dot = s.find_last_of('.');
    const size_t slash = s.find_last_of('/');
    return dot != std::string::npos && (slash == std::string::npos || dot > slash);
}

/// Candidate VFS suffixes, most specific first.
std::vector<std::string> build_candidates(const std::string& ref) {
    std::vector<std::string> out;
    if (has_extension(ref)) {
        out.push_back("/" + ref);
    } else {
        out.push_back("/materials/" + ref + ".vtf"); // Source 1 convention
        out.push_back("/" + ref + ".vtf");
        out.push_back("/" + ref + ".paa");           // Real Virtuality
        out.push_back("/" + ref + ".png");
        out.push_back("/" + ref + ".tga");
        out.push_back("/" + ref);                    // WAD3 lumps carry no extension
    }
    return out;
}

} // namespace

Ref<Texture2D> TextureLoader::load(const std::string& texture_ref) {
    if (!_vfs || texture_ref.empty()) return {};

    const std::string key = normalize_ref(texture_ref);
    if (key.empty()) return {};

    // A negative result is cached too: without it, every material referencing a
    // missing texture would rescan the entire VFS index.
    auto& cache = vfs::TextureCache::instance();
    if (cache.has_texture_entry(key)) {
        return cache.get_texture(key);
    }

    std::string resolved_uri;
    for (const auto& candidate : build_candidates(key)) {
        resolved_uri = _vfs->find_by_suffix(candidate);
        if (!resolved_uri.empty()) break;
    }

    if (resolved_uri.empty()) {
        cache.set_texture(key, Ref<Texture2D>());
        return {};
    }

    const std::vector<std::byte> bytes = _vfs->read_owned(resolved_uri);
    if (bytes.empty()) {
        cache.set_texture(key, Ref<Texture2D>());
        return {};
    }

    ir::IRTextureData ir_tex;
    bool decoded = false;

    if (resolved_uri.ends_with(".vtf")) {
        if (auto res = parsers::source1::VTFParser::parse(bytes); res.has_value()) {
            ir_tex = std::move(res.value());
            decoded = true;
        }
    } else {
        // WAD3 lumps are stored without an extension; miptex is the only other
        // format currently decodable to RGBA8.
        if (auto res = parsers::goldsrc::WAD3Parser::parse_miptex(bytes); res.has_value()) {
            ir_tex = std::move(res.value());
            decoded = true;
        }
    }

    if (!decoded) {
        UtilityFunctions::printerr("[TextureLoader] Could not decode texture: ",
                                   String(resolved_uri.c_str()));
        cache.set_texture(key, Ref<Texture2D>());
        return {};
    }

    ir_tex.name = key;
    Ref<Texture2D> texture = TextureConverter::convert(ir_tex);
    cache.set_texture(key, texture);
    return texture;
}

} // namespace quebratsk::converters
