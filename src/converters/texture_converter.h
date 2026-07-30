#pragma once

#include "../core/ir/ir_texture_data.h"

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/ref.hpp>

namespace quebratsk::converters {

/// Turns a decoded IRTextureData into a Godot ImageTexture.
///
/// This is the bridge that was missing: every texture decoder produced RGBA8 buffers
/// that nothing consumed, so imported materials were always untextured.
class TextureConverter {
public:
    /// MAIN THREAD ONLY — ImageTexture creation talks to the RenderingServer.
    /// Returns an empty Ref if the IR data is malformed.
    [[nodiscard]] static godot::Ref<godot::ImageTexture> convert(const ir::IRTextureData& ir_tex);
};

} // namespace quebratsk::converters
