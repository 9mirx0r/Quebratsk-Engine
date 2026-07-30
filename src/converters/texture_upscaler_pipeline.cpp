#include "texture_upscaler_pipeline.h"
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/core/error_macros.hpp>

#include <cstdint>

namespace quebratsk::converters {

using namespace godot;

void TextureUpscalerPipeline::_bind_methods() {
    ClassDB::bind_static_method("TextureUpscalerPipeline", D_METHOD("upscale_texture", "source_image", "target_scale_factor"), &TextureUpscalerPipeline::upscale_texture, DEFVAL(2));
}

Ref<ImageTexture> TextureUpscalerPipeline::upscale_texture(
    const Ref<Image>& source_image,
    int target_scale_factor
) {
    if (source_image.is_null() || source_image->is_empty()) return Ref<ImageTexture>();

    // target_scale_factor arrives unchecked from GDScript. Zero or negative produced
    // an invalid resize(); a large value overflowed int and/or exhausted memory.
    if (target_scale_factor < 1 || target_scale_factor > 8) {
        ERR_PRINT("[TextureUpscaler] scale_factor must be in [1, 8].");
        return Ref<ImageTexture>();
    }

    Ref<Image> upscaled = source_image->duplicate();
    if (upscaled.is_null()) return Ref<ImageTexture>();

    // resize() fails on VRAM-compressed images (DXT/BPTC/ETC), which is exactly what
    // legacy game textures decode into.
    if (upscaled->is_compressed() && upscaled->decompress() != OK) {
        ERR_PRINT("[TextureUpscaler] Cannot resize a compressed image; decompression failed.");
        return Ref<ImageTexture>();
    }

    const int64_t new_width = static_cast<int64_t>(upscaled->get_width()) * target_scale_factor;
    const int64_t new_height = static_cast<int64_t>(upscaled->get_height()) * target_scale_factor;
    if (new_width <= 0 || new_height <= 0 || new_width > 16384 || new_height > 16384) {
        ERR_PRINT("[TextureUpscaler] Result exceeds the 16384 px limit.");
        return Ref<ImageTexture>();
    }

    upscaled->resize(static_cast<int32_t>(new_width), static_cast<int32_t>(new_height),
                     Image::INTERPOLATE_LANCZOS);

    return ImageTexture::create_from_image(upscaled);
}

} // namespace quebratsk::converters
