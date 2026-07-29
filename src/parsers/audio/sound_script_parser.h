#pragma once

#include <godot_cpp/classes/audio_stream_player3d.hpp>
#include <godot_cpp/classes/ref.hpp>

#include <expected>
#include <span>
#include <string>
#include <vector>

namespace quebratsk::parsers::audio {

enum class SoundScriptParseError {
    InvalidFormat,
    FileNotFound,
};

struct SoundScriptEntry {
    std::string sound_name;
    std::string wave_file_path;
    float volume = 1.0f;
    float pitch = 1.0f;
    float attenuation_distance = 100.0f;
};

class SoundScriptParser {
public:
    /// Parse GoldSrc / Source Engine sound scripts (game_sounds.txt)
    [[nodiscard]] static std::expected<std::vector<SoundScriptEntry>, SoundScriptParseError> parse(
        std::span<const std::byte> script_bytes
    );

    /// Instantiate pre-configured Godot AudioStreamPlayer3D node
    [[nodiscard]] static godot::AudioStreamPlayer3D* create_spatial_player(
        const SoundScriptEntry& entry
    );
};

} // namespace quebratsk::parsers::audio
