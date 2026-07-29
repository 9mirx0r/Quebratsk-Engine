#include "texture_transcoder.h"
#include <thread>

namespace quebratsk::vfs {

using namespace godot;

std::future<TextureTranscoder::TranscodeResult> TextureTranscoder::enqueue_transcode(
    std::span<const std::byte> source_data,
    Format source_format,
    uint32_t width,
    uint32_t height
) {
    // Dispatch to a background thread to prevent stalling the Godot main thread
    return std::async(std::launch::async, [source_data, source_format, width, height]() -> TranscodeResult {
        switch (source_format) {
            case Format::DXT1:
                return decode_dxt1_to_rgba8(source_data, width, height);
            case Format::DXT5:
                return decode_dxt5_to_rgba8(source_data, width, height);
            default:
                // Fallback / Unsupported for now, return empty
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
    // For this alpha stub, we just return the allocated buffer to simulate the pipeline.
    
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
