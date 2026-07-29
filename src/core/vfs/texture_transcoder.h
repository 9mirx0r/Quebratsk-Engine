#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>
#include <span>
#include <future>
#include <godot_cpp/classes/image.hpp>

namespace quebratsk::vfs {

class TextureTranscoder {
public:
    enum class Format {
        DXT1,
        DXT5,
        PALETTED_8BIT,
        RGBA8
    };

    struct TranscodeResult {
        std::vector<uint8_t> decoded_data;
        uint32_t width;
        uint32_t height;
        godot::Image::Format godot_format;
    };

    /// Enqueues a texture transcoding task to a background thread pool.
    /// Used to decode legacy PC textures to universal RGBA8 for mobile/web targets.
    static std::future<TranscodeResult> enqueue_transcode(
        std::span<const std::byte> source_data,
        Format source_format,
        uint32_t width,
        uint32_t height
    );

private:
    static TranscodeResult decode_dxt1_to_rgba8(std::span<const std::byte> data, uint32_t width, uint32_t height);
    static TranscodeResult decode_dxt5_to_rgba8(std::span<const std::byte> data, uint32_t width, uint32_t height);
};

} // namespace quebratsk::vfs
