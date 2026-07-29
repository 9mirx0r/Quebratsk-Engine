#pragma once

#include "../core/ir/ir_animation_data.h"

#include <godot_cpp/classes/animation.hpp>
#include <godot_cpp/classes/ref.hpp>

namespace quebratsk::converters {

class AnimationConverter {
public:
    /// Convert Intermediate Representation IRAnimationData into native Godot 4 Animation
    [[nodiscard]] static godot::Ref<godot::Animation> convert(const ir::IRAnimationData& ir_anim);
};

} // namespace quebratsk::converters
