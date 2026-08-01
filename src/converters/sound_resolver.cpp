#include "sound_resolver.h"

#include "../core/io/keyvalues.h"

#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cctype>

namespace quebratsk::converters {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// Collect every "wave" value inside the block starting at `i` (which must be on its `{`),
/// leaving `i` on the token after the matching `}`.
///
/// Recursive because a sound with several takes nests them under rndwave, and those are the
/// interesting ones: they are why the same weapon does not sound identical on every shot.
void read_waves(const std::vector<io::KVToken>& t, size_t& i, std::vector<std::string>& waves) {
    if (i >= t.size() || t[i].text != "{") return;
    ++i;

    while (i < t.size() && t[i].text != "}") {
        if (t[i].brace) {          // a stray brace, skip rather than misread the rest
            ++i;
            continue;
        }
        const std::string key = to_lower(t[i].text);
        ++i;
        if (i >= t.size()) break;

        if (t[i].text == "{") {
            read_waves(t, i, waves);
        } else {
            if (key == "wave") waves.push_back(t[i].text);
            ++i;
        }
    }
    if (i < t.size()) ++i; // the closing brace
}

} // namespace

void SoundResolver::build_index() {
    if (_indexed || _vfs == nullptr) return;
    _indexed = true;

    // Soundscripts sit in scripts/ and have "sound" in the name, but not always the same
    // name: game_sounds_weapons.txt in one game, level_sounds_<map>.txt or a mod's own
    // <mod>_sounds.txt in another. Matching on scripts/ alone would drag in every text file
    // a game ships, so both halves are needed.
    godot::PackedStringArray extensions;
    extensions.push_back("txt");
    const godot::Dictionary hit = _vfs->find_files("scripts/", extensions,
                                                   godot::PackedStringArray(),
                                                   godot::PackedStringArray(), 0);
    const godot::PackedStringArray files = hit["files"];

    int64_t read = 0;
    for (int64_t i = 0; i < files.size(); ++i) {
        const std::string uri = files[i].utf8().get_data();
        const std::string lower = to_lower(uri);
        if (lower.find("sound") == std::string::npos) continue;
        ++read;

        const std::vector<std::byte> bytes = _vfs->read_owned(uri);
        if (bytes.empty()) continue;

        const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        const std::vector<io::KVToken> tokens = io::tokenize_keyvalues(text);

        for (size_t t = 0; t + 1 < tokens.size();) {
            if (tokens[t].brace) {
                ++t;
                continue;
            }
            const std::string name = to_lower(tokens[t].text);
            ++t;
            if (t >= tokens.size() || tokens[t].text != "{") continue;

            std::vector<std::string> waves;
            read_waves(tokens, t, waves);
            if (waves.empty()) continue;

            // First definition wins. A game and a mod can both define the same entry, and
            // the base game's is the one that matches the model that shipped with it.
            _entries.try_emplace(name, std::move(waves));
        }
    }

    godot::UtilityFunctions::print("[QuebratskSound] ", static_cast<int64_t>(_entries.size()),
                                   " soundscript entries from ", read, " script file(s)");
}

std::string SoundResolver::locate_wave(std::string wave, const std::string& origin_uri) const {
    if (_vfs == nullptr || wave.empty()) return {};

    // A leading punctuation character is a playback flag, not part of the path: ')' for
    // spatialised, '*' for streamed, '#' to bypass the mixer, and so on. They are stripped
    // because the file on disk has none of them.
    size_t start = 0;
    while (start < wave.size() && !std::isalnum(static_cast<unsigned char>(wave[start]))
           && wave[start] != '_' && wave[start] != '/' && wave[start] != '\\') {
        ++start;
    }
    wave = wave.substr(start);
    std::replace(wave.begin(), wave.end(), '\\', '/');
    if (wave.empty()) return {};

    // Paths in a soundscript are relative to sound/, which is where the archives keep them.
    std::string uri = _vfs->find_by_suffix("sound/" + to_lower(wave), origin_uri);
    if (uri.empty()) uri = _vfs->find_by_suffix("/" + to_lower(wave), origin_uri);
    return uri;
}

std::vector<std::string> SoundResolver::resolve(const std::string& event_name,
                                                const std::string& origin_uri) {
    std::vector<std::string> out;
    if (_vfs == nullptr || event_name.empty()) return out;

    // A name that is already a filename is a path, not a script entry. GoldSrc writes these
    // directly and there is nothing to look up.
    const std::string lower = to_lower(event_name);
    if (lower.ends_with(".wav") || lower.ends_with(".mp3") || lower.ends_with(".ogg")) {
        if (std::string uri = locate_wave(event_name, origin_uri); !uri.empty()) {
            out.push_back(std::move(uri));
            return out;
        }
        _missing.push_back(event_name);
        return out;
    }

    build_index();
    const auto it = _entries.find(lower);
    if (it == _entries.end()) {
        _missing.push_back(event_name);
        return out;
    }

    for (const auto& wave : it->second) {
        if (std::string uri = locate_wave(wave, origin_uri); !uri.empty()) {
            out.push_back(std::move(uri));
        }
    }
    if (out.empty()) _missing.push_back(event_name);
    return out;
}

} // namespace quebratsk::converters
