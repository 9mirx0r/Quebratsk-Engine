#include "texture_upscaler_pipeline.h"
#include <godot_cpp/variant/utility_functions.hpp>

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

    Ref<Image> upscaled = source_image->duplicate();
    int new_width = upscaled->get_width() * target_scale_factor;
    int new_height = upscaled->get_height() * target_scale_factor;

    upscaled->resize(new_width, new_height, Image::INTERPOLATE_LANCZOS);

    return ImageTexture::create_from_image(upscaled);
}

} // namespace quebratsk::converters
