#include "sound_script_parser.h"
#include <godot_cpp/core/memory.hpp>

namespace quebratsk::parsers::audio {

using namespace godot;

std::expected<std::vector<SoundScriptEntry>, SoundScriptParseError> SoundScriptParser::parse(
    std::span<const std::byte> script_bytes
) {
    if (script_bytes.empty()) {
        return std::unexpected(SoundScriptParseError::InvalidFormat);
    }

    std::vector<SoundScriptEntry> entries;
    return entries;
}

AudioStreamPlayer3D* SoundScriptParser::create_spatial_player(const SoundScriptEntry& entry) {
    AudioStreamPlayer3D* player = memnew(AudioStreamPlayer3D);
    player->set_volume_db(entry.volume);
    player->set_pitch_scale(entry.pitch);
    player->set_max_distance(entry.attenuation_distance);
    return player;
}

} // namespace quebratsk::parsers::audio
