#include "texture_loader.h"
#include "texture_converter.h"

#include "../core/vfs/texture_cache.h"
#include "../parsers/source1/vtf_parser.h"
#include "../parsers/goldsrc/wad3_parser.h"
#include "../parsers/source1/vmt_parser.h"
#include "../parsers/rv_enfusion/paa_parser.h"

#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <vector>

namespace quebratsk::converters {

using namespace godot;

namespace {

/// The references whose material asked to be see-through. Kept beside the texture cache,
/// which holds pictures only, and for as long as it does: a reference answered from the
/// cache never reaches its material script again.
std::unordered_set<std::string>& masked_refs() {
    static std::unordered_set<std::string> refs;
    return refs;
}

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

std::vector<std::byte> TextureLoader::read_material_include(std::string_view path,
                                                            const std::string& near) const {
    if (!_vfs) return {};
    std::string key = normalize_ref(std::string(path));
    if (key.empty()) return {};
    if (!has_extension(key)) key += ".vmt";

    // The path is written from the game's root, "materials/" included, but a mount may
    // start below that folder, so the part after it is tried too.
    std::vector<std::string> suffixes = {"/" + key};
    if (key.starts_with("materials/")) suffixes.push_back("/" + key.substr(10));

    const std::string& origin = near.empty() ? _origin : near;
    for (const auto& suffix : suffixes) {
        const std::string uri = _vfs->find_by_suffix(suffix, origin);
        if (!uri.empty()) return _vfs->read_owned(uri);
    }
    return {};
}

bool TextureLoader::is_masked(const std::string& texture_ref) const {
    return masked_refs().contains(normalize_ref(texture_ref));
}

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

    // What the asset itself said, before any guessing. A model states both the material's
    // name and the directories it lives in; joining them is an exact path, and searching
    // the index for a suffix is not.
    if (!_search_paths.empty() && !key.starts_with("vfs://")) {
        const bool named = has_extension(key);
        const std::string bare = named ? key.substr(0, key.find_last_of('.')) : key;
        // A reference that names its extension means that file. Trying the .vmt first for
        // "x.vtf" would find the material that asked for it and go round in a circle.
        const std::vector<std::string> exts = named
            ? std::vector<std::string>{key.substr(key.find_last_of('.'))}
            : std::vector<std::string>{".vmt", ".vtf"};
        for (const auto& dir : _search_paths) {
            for (const auto& ext : exts) {
                resolved_uri = _vfs->find_by_suffix("/" + dir + bare + ext, _origin);
                if (!resolved_uri.empty()) break;
            }
            if (!resolved_uri.empty()) break;
        }
    }

    if (resolved_uri.empty() && key.starts_with("vfs://")) {
        // Already a full VFS URI, which is what a caller holding a search result has. This
        // used to fall through to the suffix search below, where it could never match: the
        // indexed key is "vfs://mount/path" and the search asked for something *ending* in
        // "/vfs://mount/path". Every such call returned null without a word, which is why
        // the dock's texture preview quietly showed nothing.
        if (_vfs->file_exists(godot::String(key.c_str()))) {
            resolved_uri = key;
        }
    } else if (resolved_uri.empty()) {
        for (const auto& candidate : build_candidates(key)) {
            resolved_uri = _vfs->find_by_suffix(candidate, _origin);
            if (!resolved_uri.empty()) break;
        }
    }

    if (resolved_uri.empty()) {
        // Recorded rather than only logged, so a caller can tell a user which files their
        // model wanted and did not get instead of handing them something grey.
        _missing.push_back(key);
        cache.set_texture(key, Ref<Texture2D>());
        return {};
    }

    const std::vector<std::byte> bytes = _vfs->read_owned(resolved_uri);
    if (bytes.empty()) {
        UtilityFunctions::printerr("[TextureLoader] Read returned nothing: ",
                                   String(resolved_uri.c_str()));
        cache.set_texture(key, Ref<Texture2D>());
        return {};
    }

    ir::IRTextureData ir_tex;
    bool decoded = false;

    // A .vmt is a material script, not a picture: it names the image rather than being
    // one. Resolving a model's material to its .vmt and then handing those bytes to an
    // image decoder is how a correctly located texture still ends up as nothing, which is
    // what every Source model was doing. Follow the reference it holds.
    if (resolved_uri.ends_with(".vmt")) {
        // A patch names no texture of its own; it points at the material that does.
        const parsers::source1::VMTParser::IncludeReader read_include =
            [this, &resolved_uri](std::string_view path) {
                return read_material_include(path, resolved_uri);
            };
        auto material = parsers::source1::VMTParser::parse(bytes, read_include);
        if (material.has_value() && material->is_transparent) masked_refs().insert(key);
        if (material.has_value() && material->albedo_texture.has_value()) {
            std::string base = normalize_ref(material->albedo_texture->vfs_path);
            // Most materials share their picture's name: "de_shipped/deck01" names
            // "de_shipped/deck01". Asked for by that name alone it would find this .vmt
            // again, so it is asked for as the picture it is.
            const std::string self = has_extension(key) ? key.substr(0, key.find_last_of('.')) : key;
            if (base == self) base += ".vtf";
            if (!base.empty() && base != key) {
                Ref<Texture2D> image = load(base);
                cache.set_texture(key, image);
                if (image.is_null()) {
                    _missing.push_back(base);
                }
                return image;
            }
        }
        UtilityFunctions::printerr("[TextureLoader] Material names no base texture: ",
                                   String(resolved_uri.c_str()));
        _missing.push_back(key);
        cache.set_texture(key, Ref<Texture2D>());
        return {};
    }

    if (resolved_uri.ends_with(".vtf")) {
        if (auto res = parsers::source1::VTFParser::parse(bytes); res.has_value()) {
            ir_tex = std::move(res.value());
            decoded = true;
        }
    } else if (resolved_uri.ends_with(".paa")) {
        auto res = parsers::rv_enfusion::PAAParser::parse(bytes);
        if (res.has_value()) {
            ir_tex = std::move(res.value());
            decoded = true;
        } else {
            // Name the reason rather than the category. "Could not decode" for a file whose
            // mipmaps are simply compressed sends whoever reads it looking for corruption.
            using E = parsers::rv_enfusion::PAAParseError;
            const char* why = "unreadable";
            switch (res.error()) {
                case E::UnknownFormat:     why = "not a PAA:"; break;
                case E::Truncated:         why = "truncated PAA:"; break;
                case E::CompressedMipmap:  why = "PAA with compressed mipmaps, not readable yet:"; break;
                case E::UnsupportedFormat: why = "PAA in a layout with no decoder:"; break;
            }
            UtilityFunctions::printerr("[TextureLoader] ", why, " ",
                                       String(resolved_uri.c_str()));
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
        if (!resolved_uri.ends_with(".paa")) {  // the PAA branch already named its reason
            UtilityFunctions::printerr("[TextureLoader] Could not decode texture: ",
                                       String(resolved_uri.c_str()));
        }
        cache.set_texture(key, Ref<Texture2D>());
        return {};
    }

    ir_tex.name = key;
    Ref<Texture2D> texture = TextureConverter::convert(ir_tex);
    cache.set_texture(key, texture);
    return texture;
}

} // namespace quebratsk::converters
