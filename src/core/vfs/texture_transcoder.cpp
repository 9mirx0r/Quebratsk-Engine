#include "texture_transcoder.h"
#include <memory>

namespace quebratsk::vfs {

using namespace godot;

std::future<TextureTranscoder::TranscodeResult> TextureTranscoder::enqueue_transcode(
    std::span<const std::byte> source_data,
    Format source_format,
    uint32_t width,
    uint32_t height
) {
    // FIX A1: Copy bytes into an owned vector to prevent dangling span in the async lambda.
    // std::span is a non-owning view — if the caller frees the backing buffer before
    // the async thread reads it, we get use-after-free.
    auto owned_data = std::make_shared<std::vector<std::byte>>(
        source_data.begin(), source_data.end());

    return std::async(std::launch::async, [owned_data, source_format, width, height]() -> TranscodeResult {
        std::span<const std::byte> data_view(*owned_data);
        switch (source_format) {
            case Format::DXT1:
                return decode_dxt1_to_rgba8(data_view, width, height);
            case Format::DXT5:
                return decode_dxt5_to_rgba8(data_view, width, height);
            default:
                return TranscodeResult{{}, width, height, Image::FORMAT_MAX};
        }
    });
}

TextureTranscoder::TranscodeResult TextureTranscoder::decode_dxt1_to_rgba8(std::span<const std::byte> data, uint32_t width, uint32_t height) {
    TranscodeResult result;
    result.width = width;
    result.height = height;
    result.godot_format = Image::FORMAT_RGBA8;

    // Allocate buffer for uncompressed RGBA8 (4 bytes per pixel)
    result.decoded_data.resize(width * height * 4);

    // TODO: Integrate fast DXT1 decoding algorithm (e.g. stb_dxt or custom decoder)

    return result;
}

TextureTranscoder::TranscodeResult TextureTranscoder::decode_dxt5_to_rgba8(std::span<const std::byte> data, uint32_t width, uint32_t height) {
    TranscodeResult result;
    result.width = width;
    result.height = height;
    result.godot_format = Image::FORMAT_RGBA8;

    result.decoded_data.resize(width * height * 4);

    // TODO: Integrate fast DXT5 decoding algorithm

    return result;
}

} // namespace quebratsk::vfs
