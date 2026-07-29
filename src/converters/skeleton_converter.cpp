#include "skeleton_converter.h"

#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/transform3d.hpp>

namespace quebratsk::converters {

using namespace godot;

Skeleton3D* SkeletonConverter::convert(const ir::IRSkeletonData& ir_skeleton) {
    Skeleton3D* skel = memnew(Skeleton3D);

    for (const auto& bone : ir_skeleton.bones) {
        int bone_idx = skel->add_bone(StringName(bone.name.c_str()));

        if (bone.parent_index >= 0 && bone.parent_index < skel->get_bone_count()) {
            skel->set_bone_parent(bone_idx, bone.parent_index);
        }

        Transform3D rest_transform;
        rest_transform.basis = Basis(bone.rotation);
        rest_transform.origin = bone.position;

        skel->set_bone_rest(bone_idx, rest_transform);
        skel->set_bone_pose_position(bone_idx, bone.position);
        skel->set_bone_pose_rotation(bone_idx, bone.rotation);
        skel->set_bone_pose_scale(bone_idx, bone.scale);
    }

    return skel;
}

} // namespace quebratsk::converters
