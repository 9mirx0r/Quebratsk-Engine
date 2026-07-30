#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ir_mesh_data.h"
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace quebratsk::ir {

struct IRBone {
    std::string name;
    int32_t parent_index = -1;  // -1 for root joint
    godot::Vector3 position;    // Local rest position
    godot::Quaternion rotation; // Local rest rotation
    godot::Vector3 scale = godot::Vector3(1, 1, 1);
    godot::Transform3D pose_to_bone; // Model to bone inverse matrix
};

struct IRSkeletonData {
    SourceEngine source_engine = SourceEngine::GoldSrc;
    std::string name;
    std::vector<IRBone> bones;

    [[nodiscard]] int32_t find_bone(const std::string& bone_name) const {
        for (size_t i = 0; i < bones.size(); ++i) {
            if (bones[i].name == bone_name) return static_cast<int32_t>(i);
        }
        return -1;
    }
};

/// Compose each bone's model-space rest transform from the parent-relative locals.
///
/// Godot's Skin wants `bind_pose = global_rest.affine_inverse()`, and mesh vertices are
/// emitted in model space with the rest pose already baked in, so this is the bridge
/// between the two. Pure math on IR types: no Godot Object, safe on any thread.
///
/// Bones are expected parents-first (every source format used here guarantees that);
/// a forward or self reference falls back to treating the bone as a root rather than
/// reading an uninitialised transform.
[[nodiscard]] inline std::vector<godot::Transform3D>
compute_global_rest_transforms(const IRSkeletonData& skeleton) {
    std::vector<godot::Transform3D> global(skeleton.bones.size());

    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        const IRBone& bone = skeleton.bones[i];
        const godot::Transform3D local(godot::Basis(bone.rotation).scaled(bone.scale),
                                       bone.position);

        if (bone.parent_index >= 0 && static_cast<size_t>(bone.parent_index) < i) {
            global[i] = global[static_cast<size_t>(bone.parent_index)] * local;
        } else {
            global[i] = local;
        }
    }
    return global;
}

} // namespace quebratsk::ir
