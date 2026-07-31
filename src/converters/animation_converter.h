#pragma once

#include "../core/ir/ir_animation_data.h"

#include <godot_cpp/classes/animation.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/variant/string.hpp>

namespace quebratsk::converters {

class AnimationConverter {
public:
    /// Convert an IR animation into a Godot 4 Animation with per-bone position and
    /// rotation tracks.
    ///
    /// `skeleton_path` is the path from the AnimationPlayer's root node to the Skeleton3D
    /// the tracks drive. It is what makes an animation resolve against a real skeleton
    /// rather than silently animating nothing, which is the failure mode here: a track
    /// pointing at a node that does not exist plays perfectly and moves no bone. Empty
    /// means the root node *is* the skeleton, which is what this project builds, and the
    /// track path becomes ":BoneName".
    ///
    /// A track whose value never changes is collapsed to a single key. Most bones hold
    /// still through any one sequence, so this is the difference between one key and one
    /// per frame for the majority of a 68-bone skeleton.
    ///
    /// `target`, when given, drops tracks naming a bone it does not have. A GMod player
    /// model borrows its sequences from a shared animation model with its own, larger bone
    /// table, so a good fraction of the tracks address bones that are not there. They are
    /// harmless — they resolve to nothing — but they are also carried, saved and searched
    /// for the life of the animation.
    [[nodiscard]] static godot::Ref<godot::Animation> convert(
        const ir::IRAnimationData& ir_anim,
        const godot::String& skeleton_path = godot::String(),
        const godot::Skeleton3D* target = nullptr);
};

} // namespace quebratsk::converters
