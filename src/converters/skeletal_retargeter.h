#pragma once

#include "../core/ir/ir_skeleton_data.h"

#include <string>
#include <unordered_map>

namespace quebratsk::converters {

/// Standard bone names for Godot 4 SkeletonProfileHumanoid.
/// LeftShoulder/RightShoulder are REQUIRED by the profile; their absence was why
/// retargeting failed even when every other bone matched.
namespace HumanoidBones {
    inline const std::string Root = "Root";
    inline const std::string Hips = "Hips";
    inline const std::string Spine = "Spine";
    inline const std::string Chest = "Chest";
    inline const std::string UpperChest = "UpperChest";
    inline const std::string Neck = "Neck";
    inline const std::string Head = "Head";
    inline const std::string LeftShoulder = "LeftShoulder";
    inline const std::string RightShoulder = "RightShoulder";
    inline const std::string LeftToes = "LeftToes";
    inline const std::string RightToes = "RightToes";
    inline const std::string LeftUpperArm = "LeftUpperArm";
    inline const std::string LeftLowerArm = "LeftLowerArm";
    inline const std::string LeftHand = "LeftHand";
    inline const std::string RightUpperArm = "RightUpperArm";
    inline const std::string RightLowerArm = "RightLowerArm";
    inline const std::string RightHand = "RightHand";
    inline const std::string LeftUpperLeg = "LeftUpperLeg";
    inline const std::string LeftLowerLeg = "LeftLowerLeg";
    inline const std::string LeftFoot = "LeftFoot";
    inline const std::string RightUpperLeg = "RightUpperLeg";
    inline const std::string RightLowerLeg = "RightLowerLeg";
    inline const std::string RightFoot = "RightFoot";
}

class SkeletalRetargeter {
public:
    /// Map legacy bone names (Source 1 / GoldSrc / Bohemia) to Godot 4 SkeletonProfileHumanoid.
    /// Matching is case-insensitive; unmapped names are returned unchanged.
    [[nodiscard]] static std::string map_bone_name(const std::string& raw_name);

    /// Rename IRSkeletonData bones to SkeletonProfileHumanoid conventions.
    ///
    /// SCOPE: this performs naming only. Full retargeting also requires aligning each
    /// bone's rest pose with the profile's reference (T-pose) orientation and writing
    /// IRBone::pose_to_bone, neither of which is implemented — the field is declared
    /// in ir_skeleton_data.h but never written. Renaming is a prerequisite for that
    /// work, not a substitute for it. Do not assume a renamed skeleton is retargeted.
    static void retarget_to_humanoid(ir::IRSkeletonData& ir_skeleton);
};

} // namespace quebratsk::converters
