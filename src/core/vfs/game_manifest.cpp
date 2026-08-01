#include "game_manifest.h"

#include "../io/keyvalues.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace quebratsk::vfs {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string read_text(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/// The game folder a search path entry points at, or an empty string.
///
/// Source writes these as paths rather than names: "hl2/hl2_textures.vpk", "|gameinfo_path|.",
/// "cstrike". Only the first component names a game folder, and the |token| forms are the
/// game's own directory, which it does not need to be told to search.
std::string search_path_root(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');

    // Strip the |all_source_engine_paths| and |gameinfo_path| markers.
    while (!value.empty() && value.front() == '|') {
        const size_t close = value.find('|', 1);
        if (close == std::string::npos) return {};
        value = value.substr(close + 1);
    }
    while (!value.empty() && value.front() == '/') value = value.substr(1);

    const size_t slash = value.find('/');
    std::string root = slash == std::string::npos ? value : value.substr(0, slash);
    root = to_lower(root);

    // "." is the game's own folder and "platform" is the engine's shared UI content, which
    // holds no game assets worth falling back to.
    if (root == "." || root == ".." || root == "platform" || root == "bin") return {};
    return root;
}

/// A game directory as it is identified everywhere else: absolute, lowercase, forward slashes.
std::string canonical_dir(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::path abs = std::filesystem::absolute(dir, ec);
    if (ec) abs = dir;
    std::string out = abs.lexically_normal().string();
    std::replace(out.begin(), out.end(), '\\', '/');
    while (!out.empty() && out.back() == '/') out.pop_back();
    return to_lower(out);
}

/// Turn a fallback's name into the directory it means.
///
/// A manifest names its fallbacks the way the engine does, as bare folder names sitting
/// beside the game: "valve" next to "cstrike", "hl2" next to CS Source's own "cstrike",
/// "sourceengine" next to "garrysmod". Resolving them against the game's parent is what
/// keeps two identically named games apart. One that is not on this machine is dropped
/// rather than kept as a path that answers nothing.
void add_fallback(GameManifest& manifest, const std::filesystem::path& game_dir,
                  const std::string& name) {
    if (name.empty()) return;

    std::error_code ec;
    const std::filesystem::path candidate = game_dir.parent_path() / name;
    if (!std::filesystem::is_directory(candidate, ec)) return;

    std::string id = canonical_dir(candidate);
    if (id == manifest.id) return;
    if (std::find(manifest.fallbacks.begin(), manifest.fallbacks.end(), id)
        == manifest.fallbacks.end()) {
        manifest.fallbacks.push_back(std::move(id));
    }
}

/// Source: a KeyValues file whose FileSystem/SearchPaths block is the search order itself.
std::optional<GameManifest> read_gameinfo(const std::string& dir, const std::string& text) {
    const std::vector<io::KVToken> tokens = io::tokenize_keyvalues(text);

    const std::filesystem::path game_dir(dir);
    GameManifest manifest;
    manifest.id = canonical_dir(game_dir);
    manifest.name = to_lower(game_dir.filename().string());

    // Find the SearchPaths block and read the pairs inside it. Nesting is not tracked beyond
    // that: the block holds only key/value pairs, so once inside, the next closing brace ends
    // it and nothing between can be misread as a game folder.
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i].brace || to_lower(tokens[i].text) != "searchpaths") continue;
        if (tokens[i + 1].text != "{") continue;

        for (size_t j = i + 2; j + 1 < tokens.size() && tokens[j].text != "}"; j += 2) {
            if (tokens[j].brace || tokens[j + 1].brace) break;

            const std::string key = to_lower(tokens[j].text);
            // gamebin points at compiled code and mod_write / game_write at save locations.
            if (key.find("bin") != std::string::npos) continue;

            add_fallback(manifest, game_dir, search_path_root(tokens[j + 1].text));
        }
        break;
    }
    return manifest;
}

/// GoldSrc: a flat list of key/value pairs, of which one matters.
std::optional<GameManifest> read_liblist(const std::string& dir, const std::string& text) {
    const std::vector<io::KVToken> tokens = io::tokenize_keyvalues(text);

    const std::filesystem::path game_dir(dir);
    GameManifest manifest;
    manifest.id = canonical_dir(game_dir);
    manifest.name = to_lower(game_dir.filename().string());

    std::string base;
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        const std::string key = to_lower(tokens[i].text);
        if (key == "fallback_dir") {
            add_fallback(manifest, game_dir, search_path_root(tokens[i + 1].text));
        } else if (key == "basedir") {
            base = search_path_root(tokens[i + 1].text);
        }
    }

    // GoldSrc does not write down the game every mod is built on, because the engine already
    // knows it: a mod that declares nothing still reads Half-Life's own content. Counter-Strike
    // is the case that matters here, and its liblist.gam names no fallback at all. Leaving it
    // implicit here would leave every Counter-Strike model unable to find a Half-Life texture.
    if (base.empty()) base = "valve";
    add_fallback(manifest, game_dir, base);
    return manifest;
}

} // namespace

std::optional<GameManifest> read_game_manifest(const std::string& dir) {
    std::error_code ec;
    const std::filesystem::path base(dir);

    // A folder with one of these names is content, whatever it happens to contain. Valve
    // ships a stray liblist.gam inside scripts/ in more than one game, and without this a
    // directory called "scripts" becomes a game of its own, taking every soundscript out of
    // the search order of the game that ships it.
    static const std::array<const char*, 8> kContentFolders = {
        "scripts", "models", "sound", "maps", "materials", "sprites", "gfx", "media",
    };
    const std::string own_name = to_lower(base.filename().string());
    for (const char* reserved : kContentFolders) {
        if (own_name == reserved) return std::nullopt;
    }

    // gameinfo.txt first: a Source game that also ships a liblist.gam for legacy tools would
    // otherwise be read through the poorer of its two manifests.
    for (const char* name : {"gameinfo.txt", "liblist.gam"}) {
        const std::filesystem::path file = base / name;
        if (!std::filesystem::is_regular_file(file, ec)) continue;

        const std::string text = read_text(file);
        if (text.empty()) continue;

        return std::string(name) == "gameinfo.txt" ? read_gameinfo(dir, text)
                                                   : read_liblist(dir, text);
    }
    return std::nullopt;
}

std::optional<GameManifest> find_owning_game(const std::string& real_path) {
    std::error_code ec;
    std::filesystem::path dir(real_path);

    // Start at the containing folder when handed a file. An archive is a file; a mounted
    // tree is already a directory.
    if (std::filesystem::is_regular_file(dir, ec)) dir = dir.parent_path();

    // The OUTERMOST manifest wins, not the first one found on the way up. Valve leaves a
    // liblist.gam inside hl2/scripts and inside sourceengine/scripts, neither of which is a
    // game: taking the innermost made "scripts" a game of its own, and put every soundscript
    // outside the search order of the game that ships it.
    //
    // A bound is what stops the walk from climbing out to the drive root on a path that has
    // no manifest anywhere. Eight levels covers a workshop addon buried in subfolders, and
    // the answer is cached per directory, so the extra steps are paid once.
    std::optional<GameManifest> best;
    for (int level = 0; level < 8 && !dir.empty(); ++level) {
        if (auto manifest = read_game_manifest(dir.string()); manifest.has_value()) {
            best = std::move(manifest);
        }
        const std::filesystem::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return best;
}

} // namespace quebratsk::vfs
