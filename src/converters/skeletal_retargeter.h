#pragma once

#include "../core/ir/ir_skeleton_data.h"

#include <string>
#include <unordered_map>

namespace quebratsk::converters {

/// Standard bone names for Godot 4 SkeletonProfileHumanoid
namespace HumanoidBones {
    inline const std::string Root = "Root";
    inline const std::string Hips = "Hips";
    inline const std::string Spine = "Spine";
    inline const std::string Chest = "Chest";
    inline const std::string Neck = "Neck";
    inline const std::string Head = "Head";
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
    /// Map legacy bone names (Source 1 / GoldSrc / Bohemia) to Godot 4 SkeletonProfileHumanoid
    [[nodiscard]] static std::string map_bone_name(const std::string& raw_name);

    /// Retarget IRSkeletonData bones into SkeletonProfileHumanoid naming & T-Pose rest angles
    static void retarget_to_humanoid(ir::IRSkeletonData& ir_skeleton);
};

} // namespace quebratsk::converters
