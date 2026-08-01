#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace quebratsk::io {

/// One token of a KeyValues file: a quoted string, a bare word, or a brace.
struct KVToken {
    std::string text;
    bool brace = false;
};

/// Split a KeyValues file into tokens.
///
/// Valve uses this one text format for most of what it does not compile: soundscripts,
/// gameinfo.txt, material definitions, mod manifests. It is small enough to read directly,
/// being quoted strings, bare words, braces and // comments to end of line, and writing a
/// general parser would mean handling #base includes and platform conditionals that none of
/// the files read here use.
///
/// Structure is left to the caller because these files disagree about it: gameinfo.txt nests
/// three levels deep and liblist.gam is a flat list of pairs with no braces at all.
[[nodiscard]] std::vector<KVToken> tokenize_keyvalues(std::string_view text);

} // namespace quebratsk::io
