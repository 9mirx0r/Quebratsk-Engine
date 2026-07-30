#pragma once

#include "../core/ir/ir_skeleton_data.h"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/classes/skin.hpp>

namespace quebratsk::converters {

class SkeletonConverter {
public:
    /// Build a Godot 4 Skeleton3D from the IR skeleton.
    ///
    /// MAIN THREAD ONLY. The returned node is unparented and owned by the caller: add
    /// it to the tree or free it.
    [[nodiscard]] static godot::Skeleton3D* convert(const ir::IRSkeletonData& ir_skeleton);

    /// Build the Skin that binds a mesh's bone indices to the skeleton.
    ///
    /// Mesh vertices are emitted in model space with the rest pose baked in, so each
    /// bind pose is the inverse of that bone's global rest transform. Without this a
    /// mesh with ARRAY_BONES/ARRAY_WEIGHTS renders collapsed at the origin.
    ///
    /// Bind index equals bone index, which is what the parsers write into
    /// IRSurface::bone_indices.
    [[nodiscard]] static godot::Ref<godot::Skin> make_skin(const ir::IRSkeletonData& ir_skeleton);

    /// Drive the skeleton into one of the poses recovered from the asset's animation
    /// sequences, leaving the rest pose (and therefore the Skin's bind poses) untouched.
    ///
    /// Only the *pose* is changed, never the rest: the Skin binds against the rest, so
    /// overwriting it here would move the mesh twice.
    ///
    /// Returns false when the pose does not match the skeleton's bone count.
    static bool apply_pose(godot::Skeleton3D* skeleton, const ir::IRPose& pose);
};

} // namespace quebratsk::converters
