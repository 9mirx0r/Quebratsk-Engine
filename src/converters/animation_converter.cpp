#include "animation_converter.h"

#include <godot_cpp/variant/node_path.hpp>

namespace quebratsk::converters {

using namespace godot;

namespace {

/// Whether every keyframe holds the same value, within the precision the source format
/// actually carries. Source quantises rotations to 15 bits and positions to a scaled
/// 16-bit integer, so exact equality would rarely hold even for a bone the sequence
/// leaves alone.
bool position_is_constant(const std::vector<ir::IRAnimKeyframe>& keys) {
    for (size_t i = 1; i < keys.size(); ++i) {
        if (!keys[i].position.is_equal_approx(keys[0].position)) return false;
    }
    return true;
}

bool rotation_is_constant(const std::vector<ir::IRAnimKeyframe>& keys) {
    for (size_t i = 1; i < keys.size(); ++i) {
        if (!keys[i].rotation.is_equal_approx(keys[0].rotation)) return false;
    }
    return true;
}

} // namespace

Ref<Animation> AnimationConverter::convert(const ir::IRAnimationData& ir_anim,
                                           const String& skeleton_path,
                                           const Skeleton3D* target) {
    Ref<Animation> anim;
    anim.instantiate();

    // A single-frame sequence has no duration, and Godot treats a zero-length animation
    // as one that never advances.
    anim->set_length(ir_anim.duration > 0.0f ? ir_anim.duration : 0.001f);
    anim->set_step(1.0f / (ir_anim.fps > 0.0f ? ir_anim.fps : 30.0f));
    anim->set_loop_mode(ir_anim.looping ? Animation::LOOP_LINEAR : Animation::LOOP_NONE);

    for (const auto& track : ir_anim.bone_tracks) {
        if (track.bone_name.empty() || track.keyframes.empty()) continue;

        const String bone(track.bone_name.c_str());
        if (target != nullptr && const_cast<Skeleton3D*>(target)->find_bone(bone) < 0) continue;

        const NodePath path(skeleton_path + String(":") + bone);

        const int pos_track = anim->add_track(Animation::TYPE_POSITION_3D);
        anim->track_set_path(pos_track, path);
        if (position_is_constant(track.keyframes)) {
            anim->position_track_insert_key(pos_track, 0.0, track.keyframes[0].position);
        } else {
            for (const auto& kf : track.keyframes) {
                anim->position_track_insert_key(pos_track, kf.time, kf.position);
            }
        }

        const int rot_track = anim->add_track(Animation::TYPE_ROTATION_3D);
        anim->track_set_path(rot_track, path);
        if (rotation_is_constant(track.keyframes)) {
            anim->rotation_track_insert_key(rot_track, 0.0, track.keyframes[0].rotation);
        } else {
            for (const auto& kf : track.keyframes) {
                anim->rotation_track_insert_key(rot_track, kf.time, kf.rotation);
            }
        }
    }

    return anim;
}

} // namespace quebratsk::converters
