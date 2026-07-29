#include "animation_converter.h"

#include <godot_cpp/variant/node_path.hpp>

namespace quebratsk::converters {

using namespace godot;

Ref<Animation> AnimationConverter::convert(const ir::IRAnimationData& ir_anim) {
    Ref<Animation> anim;
    anim.instantiate();

    anim->set_length(ir_anim.duration);
    anim->set_step(1.0f / (ir_anim.fps > 0.0f ? ir_anim.fps : 30.0f));
    anim->set_loop_mode(ir_anim.looping ? Animation::LOOP_LINEAR : Animation::LOOP_NONE);

    for (const auto& track : ir_anim.bone_tracks) {
        String track_path_str = "%GeneralSkeleton:" + String(track.bone_name.c_str());

        // Position track
        int pos_track_idx = anim->add_track(Animation::TYPE_POSITION_3D);
        anim->track_set_path(pos_track_idx, NodePath(track_path_str));

        // Rotation track
        int rot_track_idx = anim->add_track(Animation::TYPE_ROTATION_3D);
        anim->track_set_path(rot_track_idx, NodePath(track_path_str));

        for (const auto& kf : track.keyframes) {
            anim->position_track_insert_key(pos_track_idx, kf.time, kf.position);
            anim->rotation_track_insert_key(rot_track_idx, kf.time, kf.rotation);
        }
    }

    return anim;
}

} // namespace quebratsk::converters
