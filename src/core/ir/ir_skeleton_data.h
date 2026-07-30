#pragma once

#include <cstdint>
#include <string>
#include <string_view>
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

/// A single skeletal pose: one parent-relative transform per bone.
///
/// Source and GoldSrc models ship in a bind pose (usually a T-pose), which is a
/// modelling artefact rather than anything the game ever displays. The real stances —
/// idle, crouched, holding a weapon — live in the format's animation sequences. A pose
/// here is one frame lifted out of such a sequence.
///
/// `positions` and `rotations` are parallel to IRSkeletonData::bones.
struct IRPose {
    std::string name;   // source sequence label, e.g. "idle_all_01"
    std::vector<godot::Vector3> positions;
    std::vector<godot::Quaternion> rotations;

    [[nodiscard]] bool matches(size_t bone_count) const {
        return positions.size() == bone_count && rotations.size() == bone_count;
    }
};

struct IRSkeletonData {
    SourceEngine source_engine = SourceEngine::GoldSrc;
    std::string name;
    std::vector<IRBone> bones;

    /// Poses recovered from the asset's animation sequences, in file order.
    std::vector<IRPose> poses;

    /// Find a pose by its exact sequence label, or -1. Prefer this over find_pose() when
    /// acting on a name a caller supplied: substrings collide badly here, since Source
    /// names the crouched variant of "idle_smg1" simply "cidle_smg1".
    [[nodiscard]] int32_t find_pose_exact(std::string_view name) const {
        for (size_t i = 0; i < poses.size(); ++i) {
            if (poses[i].name == name) return static_cast<int32_t>(i);
        }
        return -1;
    }

    /// Find a pose whose name contains `needle` (case-sensitive), or -1.
    [[nodiscard]] int32_t find_pose(std::string_view needle) const {
        for (size_t i = 0; i < poses.size(); ++i) {
            if (poses[i].name.find(needle) != std::string::npos) {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }

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
