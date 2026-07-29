#include "skeletal_retargeter.h"
#include <algorithm>

namespace quebratsk::converters {

static const std::unordered_map<std::string, std::string> kBoneMap = {
    // Valve Source 1 Biped Map
    {"ValveBiped.Bip01_Pelvis", HumanoidBones::Hips},
    {"ValveBiped.Bip01_Spine", HumanoidBones::Spine},
    {"ValveBiped.Bip01_Spine2", HumanoidBones::Chest},
    {"ValveBiped.Bip01_Neck1", HumanoidBones::Neck},
    {"ValveBiped.Bip01_Head1", HumanoidBones::Head},
    {"ValveBiped.Bip01_L_UpperArm", HumanoidBones::LeftUpperArm},
    {"ValveBiped.Bip01_L_Forearm", HumanoidBones::LeftLowerArm},
    {"ValveBiped.Bip01_L_Hand", HumanoidBones::LeftHand},
    {"ValveBiped.Bip01_R_UpperArm", HumanoidBones::RightUpperArm},
    {"ValveBiped.Bip01_R_Forearm", HumanoidBones::RightLowerArm},
    {"ValveBiped.Bip01_R_Hand", HumanoidBones::RightHand},
    {"ValveBiped.Bip01_L_Thigh", HumanoidBones::LeftUpperLeg},
    {"ValveBiped.Bip01_L_Calf", HumanoidBones::LeftLowerLeg},
    {"ValveBiped.Bip01_L_Foot", HumanoidBones::LeftFoot},
    {"ValveBiped.Bip01_R_Thigh", HumanoidBones::RightUpperLeg},
    {"ValveBiped.Bip01_R_Calf", HumanoidBones::RightLowerLeg},
    {"ValveBiped.Bip01_R_Foot", HumanoidBones::RightFoot},

    // GoldSrc Biped Map
    {"bip01_pelvis", HumanoidBones::Hips},
    {"bip01_spine", HumanoidBones::Spine},
    {"bip01_neck", HumanoidBones::Neck},
    {"bip01_head", HumanoidBones::Head},
    {"bip01_l_arm", HumanoidBones::LeftUpperArm},
    {"bip01_l_elbow", HumanoidBones::LeftLowerArm},
    {"bip01_l_hand", HumanoidBones::LeftHand},
    {"bip01_r_arm", HumanoidBones::RightUpperArm},
    {"bip01_r_elbow", HumanoidBones::RightLowerArm},
    {"bip01_r_hand", HumanoidBones::RightHand},

    // Bohemia Interactive Arma/DayZ Map
    {"pelvis", HumanoidBones::Hips},
    {"spine", HumanoidBones::Spine},
    {"spine3", HumanoidBones::Chest},
    {"neck", HumanoidBones::Neck},
    {"head", HumanoidBones::Head},
    {"leftarm", HumanoidBones::LeftUpperArm},
    {"leftforearm", HumanoidBones::LeftLowerArm},
    {"lefthand", HumanoidBones::LeftHand},
    {"rightarm", HumanoidBones::RightUpperArm},
    {"rightforearm", HumanoidBones::RightLowerArm},
    {"righthand", HumanoidBones::RightHand},
    {"leftleg", HumanoidBones::LeftUpperLeg},
    {"leftlegup", HumanoidBones::LeftLowerLeg},
    {"leftfoot", HumanoidBones::LeftFoot},
    {"rightleg", HumanoidBones::RightUpperLeg},
    {"rightlegup", HumanoidBones::RightLowerLeg},
    {"rightfoot", HumanoidBones::RightFoot},
};

std::string SkeletalRetargeter::map_bone_name(const std::string& raw_name) {
    auto it = kBoneMap.find(raw_name);
    if (it != kBoneMap.end()) {
        return it->second;
    }

    // Attempt case-insensitive match
    std::string lower_raw = raw_name;
    std::transform(lower_raw.begin(), lower_raw.end(), lower_raw.begin(), [](unsigned char c) { return std::tolower(c); });

    for (const auto& [key, val] : kBoneMap) {
        std::string lower_key = key;
        std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(), [](unsigned char c) { return std::tolower(c); });
        if (lower_raw == lower_key) {
            return val;
        }
    }

    return raw_name; // Keep original if no retargeting alias found
}

void SkeletalRetargeter::retarget_to_humanoid(ir::IRSkeletonData& ir_skeleton) {
    for (auto& bone : ir_skeleton.bones) {
        bone.name = map_bone_name(bone.name);
    }
}

} // namespace quebratsk::converters
