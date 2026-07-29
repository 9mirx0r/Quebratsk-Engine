#pragma once

#include <godot_cpp/classes/audio_stream_wav.hpp>
#include <godot_cpp/classes/ref.hpp>

#include <expected>
#include <span>

namespace quebratsk::parsers::audio {

enum class AudioParseError {
    InvalidHeader,
    UnsupportedFormat,
};

class AudioDecoder {
public:
    /// Decode raw .wav sound bytes from VFS into native Godot AudioStreamWAV
    [[nodiscard]] static std::expected<godot::Ref<godot::AudioStreamWAV>, AudioParseError> decode_wav(
        std::span<const std::byte> audio_bytes
    );
};

} // namespace quebratsk::parsers::audio
