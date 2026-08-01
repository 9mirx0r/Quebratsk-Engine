#pragma once

#include <optional>
#include <string>
#include <vector>

namespace quebratsk::vfs {

/// Where a game says its content comes from.
///
/// Every Valve game ships a manifest declaring which other game folders it draws on, and the
/// order matters: Counter-Strike falls back to Half-Life, Counter-Strike Source falls back to
/// Half-Life 2, a Garry's Mod addon falls back to Garry's Mod and then to Half-Life 2. That
/// chain is the difference between a lookup that answers "the texture this model was built
/// against" and one that answers "some file with a matching name, from whichever game the
/// hash table happened to reach first".
///
/// A game is identified by WHERE it is, not by what it is called. Counter-Strike 1.6 and
/// Counter-Strike Source both live in a folder named "cstrike", and treating those as one
/// game is how a 1.6 model ends up being answered with a Source file: the two would share a
/// search order and be mutually visible. The folder name is kept only for display.
struct GameManifest {
    std::string id;                      // absolute game directory, lowercase, forward slashes
    std::string name;                    // the folder's own name: "cstrike"
    std::vector<std::string> fallbacks;  // absolute directories it draws from, in order
};

/// Read the manifest of the game directory `dir`, or nothing when it holds none.
///
/// Handles both spellings. GoldSrc writes liblist.gam, a flat list of key/value pairs with a
/// single `fallback_dir`. Source writes gameinfo.txt, a KeyValues file whose FileSystem block
/// lists a whole search path in priority order.
[[nodiscard]] std::optional<GameManifest> read_game_manifest(const std::string& dir);

/// Walk up from a file to the game directory that owns it and read its manifest.
///
/// An archive is rarely at the top of its game: a Garry's Mod addon sits two levels down in
/// addons/, a Source game's VPK sits beside the manifest. Walking up is how the file itself
/// says which game it belongs to, rather than the caller having to be told.
[[nodiscard]] std::optional<GameManifest> find_owning_game(const std::string& real_path);

} // namespace quebratsk::vfs
