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
    ///
    /// `origin_uri` is the model that named the sound. A weapon and its gunshot ship in the
    /// same game, so knowing which model asked is what stops a Counter-Strike rifle from
    /// being answered with a Half-Life 2 file of the same name.
    [[nodiscard]] std::vector<std::string> resolve(const std::string& event_name,
                                                  const std::string& origin_uri = {});

    /// Names that were looked up and led nowhere, in the order they came up.
    [[nodiscard]] const std::vector<std::string>& missing() const { return _missing; }

private:
    /// Read every sentences.txt the mounted games ship.
    ///
    /// A sentence is not a file. It is a line naming a directory and then a list of words,
    /// each of which is a separate .wav, spoken one after another: HG_ALERT1 is hgrunt/clik,
    /// hgrunt/target and hgrunt/clik again. Playing one means playing all of them in order,
    /// which is why resolve() returns a list rather than a file.
    void build_sentences();

    /// Read every soundscript the mounted games ship. Done once, on the first lookup that
    /// needs it, so a model whose sounds are plain paths never pays for it.
    void build_index();

    /// The VFS URI for a wave path as a soundscript writes it, or an empty string.
    [[nodiscard]] std::string locate_wave(std::string wave, const std::string& origin_uri) const;

    vfs::VFSManager* _vfs = nullptr;
    bool _indexed = false;
    std::unordered_map<std::string, std::vector<std::string>> _entries; // lowercased name

    /// Sentence name to the words that make it up, as paths relative to sound/.
    bool _sentences_read = false;
    std::unordered_map<std::string, std::vector<std::string>> _sentences;
    std::vector<std::string> _missing;
};

} // namespace quebratsk::converters
