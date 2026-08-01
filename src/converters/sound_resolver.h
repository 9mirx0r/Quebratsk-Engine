#pragma once

#include "../core/vfs/vfs_manager.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace quebratsk::converters {

/// Turns the sound name in a model's animation event into a file that can actually be read.
///
/// Two eras name sounds differently and neither names a path that exists as written:
///
///   GoldSrc  "weapons/ak47-1.wav"     relative to sound/, so nearly a path
///   Source   "Weapon_357.Single"      an entry in a soundscript, not a file at all
///
/// The Source case is the one that matters, because it is every modern weapon. The engine
/// resolves those names through scripts/game_sounds_weapons.txt and friends, which do ship
/// with the games as plain KeyValues text inside the VPKs. Following them is the difference
/// between a revolver that sounds like a revolver and a revolver playing whichever gunshot
/// happened to be found first.
class SoundResolver {
public:
    explicit SoundResolver(vfs::VFSManager* vfs) : _vfs(vfs) {}

    /// Every audio file the given event name refers to, as VFS URIs, in declared order.
    ///
    /// A soundscript entry often lists several takes under rndwave for variety, so this
    /// returns all of them and lets the caller pick. Empty when nothing resolves.
    [[nodiscard]] std::vector<std::string> resolve(const std::string& event_name);

    /// Names that were looked up and led nowhere, in the order they came up.
    [[nodiscard]] const std::vector<std::string>& missing() const { return _missing; }

private:
    /// Read every soundscript the mounted games ship. Done once, on the first lookup that
    /// needs it, so a model whose sounds are plain paths never pays for it.
    void build_index();

    /// The VFS URI for a wave path as a soundscript writes it, or an empty string.
    [[nodiscard]] std::string locate_wave(std::string wave) const;

    vfs::VFSManager* _vfs = nullptr;
    bool _indexed = false;
    std::unordered_map<std::string, std::vector<std::string>> _entries; // lowercased name
    std::vector<std::string> _missing;
};

} // namespace quebratsk::converters
