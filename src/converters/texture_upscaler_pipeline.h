#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace quebratsk::converters {

/// Dynamic Texture Super-Resolution & PBR Upscaler Pipeline
class TextureUpscalerPipeline : public godot::Object {
    GDCLASS(TextureUpscalerPipeline, godot::Object)

protected:
    static void _bind_methods();

public:
    TextureUpscalerPipeline() = default;
    ~TextureUpscalerPipeline() override = default;

    /// Upscales legacy low-resolution textures (e.g. 64x64 WAD3/VTF) to high-resolution ImageTextures
    static godot::Ref<godot::ImageTexture> upscale_texture(
        const godot::Ref<godot::Image>& source_image,
        int target_scale_factor = 2
    );
};

} // namespace quebratsk::converters
