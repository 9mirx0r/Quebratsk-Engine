#include "skeleton_converter.h"

#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/transform3d.hpp>

namespace quebratsk::converters {

using namespace godot;

Skeleton3D* SkeletonConverter::convert(const ir::IRSkeletonData& ir_skeleton) {
    Skeleton3D* skel = memnew(Skeleton3D);

    for (const auto& bone : ir_skeleton.bones) {
        const int bone_idx = skel->add_bone(String(bone.name.c_str()));

        // Compare against bone_idx, not get_bone_count(): the latter already counts the
        // bone just added, so a malformed parent_index equal to this bone's own index
        // would make it its own parent.
        if (bone.parent_index >= 0 && bone.parent_index < bone_idx) {
            skel->set_bone_parent(bone_idx, bone.parent_index);
        }

        // Godot bone rests are parent-relative, which is exactly how the IR stores them.
        Transform3D rest_transform;
        rest_transform.basis = Basis(bone.rotation).scaled(bone.scale);
        rest_transform.origin = bone.position;

        skel->set_bone_rest(bone_idx, rest_transform);
        skel->set_bone_pose_position(bone_idx, bone.position);
        skel->set_bone_pose_rotation(bone_idx, bone.rotation);
        skel->set_bone_pose_scale(bone_idx, bone.scale);
    }

    return skel;
}

bool SkeletonConverter::apply_pose(Skeleton3D* skeleton, const ir::IRPose& pose) {
    if (!skeleton) return false;

    const size_t bone_count = static_cast<size_t>(skeleton->get_bone_count());
    if (!pose.matches(bone_count)) return false;

    for (size_t i = 0; i < bone_count; ++i) {
        const int32_t b = static_cast<int32_t>(i);
        skeleton->set_bone_pose_position(b, pose.positions[i]);
        skeleton->set_bone_pose_rotation(b, pose.rotations[i]);
    }
    return true;
}

Ref<Skin> SkeletonConverter::make_skin(const ir::IRSkeletonData& ir_skeleton) {
    if (ir_skeleton.bones.empty()) {
        return {};
    }

    const std::vector<Transform3D> global_rest =
        ir::compute_global_rest_transforms(ir_skeleton);

    Ref<Skin> skin;
    skin.instantiate();

    for (size_t i = 0; i < ir_skeleton.bones.size(); ++i) {
        skin->add_bind(static_cast<int32_t>(i), global_rest[i].affine_inverse());
        skin->set_bind_name(static_cast<int32_t>(i),
                            StringName(ir_skeleton.bones[i].name.c_str()));
    }

    return skin;
}

} // namespace quebratsk::converters
