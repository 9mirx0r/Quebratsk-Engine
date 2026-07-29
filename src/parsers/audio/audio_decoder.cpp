#include "audio_decoder.h"

namespace quebratsk::parsers::audio {

using namespace godot;

std::expected<Ref<AudioStreamWAV>, AudioParseError> AudioDecoder::decode_wav(
    std::span<const std::byte> audio_bytes
) {
    if (audio_bytes.size() < 44) {
        return std::unexpected(AudioParseError::InvalidHeader);
    }

    Ref<AudioStreamWAV> stream;
    stream.instantiate();

    PackedByteArray pba;
    pba.resize(static_cast<int64_t>(audio_bytes.size() - 44));
    for (size_t i = 44; i < audio_bytes.size(); ++i) {
        pba.set(static_cast<int64_t>(i - 44), static_cast<uint8_t>(audio_bytes[i]));
    }

    stream->set_data(pba);
    return stream;
}

} // namespace quebratsk::parsers::audio
