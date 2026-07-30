#include "skeletal_retargeter.h"
#include <algorithm>
#include <cctype>

namespace quebratsk::converters {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

/// Keys are stored pre-lowercased so lookup is a single hash probe. The previous
/// version lowercased every key in the map on every miss, which was O(N) string
/// allocations per bone.
static const std::unordered_map<std::string, std::string> kBoneMap = {
    // --- Valve Source 1 (ValveBiped) ---
    {"valvebiped.bip01_pelvis",     HumanoidBones::Hips},
    {"valvebiped.bip01_spine",      HumanoidBones::Spine},
    {"valvebiped.bip01_spine1",     HumanoidBones::Spine},
    {"valvebiped.bip01_spine2",     HumanoidBones::Chest},
    {"valvebiped.bip01_spine4",     HumanoidBones::UpperChest},
    {"valvebiped.bip01_neck1",      HumanoidBones::Neck},
    {"valvebiped.bip01_head1",      HumanoidBones::Head},
    // Clavicles are mandatory for SkeletonProfileHumanoid and were missing entirely.
    {"valvebiped.bip01_l_clavicle", HumanoidBones::LeftShoulder},
    {"valvebiped.bip01_r_clavicle", HumanoidBones::RightShoulder},
    {"valvebiped.bip01_l_upperarm", HumanoidBones::LeftUpperArm},
    {"valvebiped.bip01_l_forearm",  HumanoidBones::LeftLowerArm},
    {"valvebiped.bip01_l_hand",     HumanoidBones::LeftHand},
    {"valvebiped.bip01_r_upperarm", HumanoidBones::RightUpperArm},
    {"valvebiped.bip01_r_forearm",  HumanoidBones::RightLowerArm},
    {"valvebiped.bip01_r_hand",     HumanoidBones::RightHand},
    {"valvebiped.bip01_l_thigh",    HumanoidBones::LeftUpperLeg},
    {"valvebiped.bip01_l_calf",     HumanoidBones::LeftLowerLeg},
    {"valvebiped.bip01_l_foot",     HumanoidBones::LeftFoot},
    {"valvebiped.bip01_l_toe0",     HumanoidBones::LeftToes},
    {"valvebiped.bip01_r_thigh",    HumanoidBones::RightUpperLeg},
    {"valvebiped.bip01_r_calf",     HumanoidBones::RightLowerLeg},
    {"valvebiped.bip01_r_foot",     HumanoidBones::RightFoot},
    {"valvebiped.bip01_r_toe0",     HumanoidBones::RightToes},

    // --- GoldSrc (Half-Life 1 biped) ---
    {"bip01_pelvis",   HumanoidBones::Hips},
    {"bip01_spine",    HumanoidBones::Spine},
    {"bip01_spine1",   HumanoidBones::Spine},
    {"bip01_spine2",   HumanoidBones::Chest},
    {"bip01_neck",     HumanoidBones::Neck},
    {"bip01_head",     HumanoidBones::Head},
    {"bip01_l_clavicle", HumanoidBones::LeftShoulder},
    {"bip01_r_clavicle", HumanoidBones::RightShoulder},
    {"bip01_l_arm",    HumanoidBones::LeftUpperArm},
    {"bip01_l_elbow",  HumanoidBones::LeftLowerArm},
    {"bip01_l_hand",   HumanoidBones::LeftHand},
    {"bip01_r_arm",    HumanoidBones::RightUpperArm},
    {"bip01_r_elbow",  HumanoidBones::RightLowerArm},
    {"bip01_r_hand",   HumanoidBones::RightHand},
    {"bip01_l_thigh",  HumanoidBones::LeftUpperLeg},
    {"bip01_l_calf",   HumanoidBones::LeftLowerLeg},
    {"bip01_l_foot",   HumanoidBones::LeftFoot},
    {"bip01_r_thigh",  HumanoidBones::RightUpperLeg},
    {"bip01_r_calf",   HumanoidBones::RightLowerLeg},
    {"bip01_r_foot",   HumanoidBones::RightFoot},

    // --- Bohemia Interactive (Arma / DayZ) ---
    // The rig uses the Max Biped convention: "<side>UpLeg" is the thigh and
    // "<side>Leg" is the calf. The old table mapped "leftleg" to the UPPER leg and
    // invented a "leftlegup" bone, so both leg joints were wrong.
    {"pelvis",        HumanoidBones::Hips},
    {"spine",         HumanoidBones::Spine},
    {"spine1",        HumanoidBones::Spine},
    {"spine2",        HumanoidBones::Chest},
    {"spine3",        HumanoidBones::UpperChest},
    {"neck",          HumanoidBones::Neck},
    {"neck1",         HumanoidBones::Neck},
    {"head",          HumanoidBones::Head},
    {"leftshoulder",  HumanoidBones::LeftShoulder},
    {"rightshoulder", HumanoidBones::RightShoulder},
    {"leftarm",       HumanoidBones::LeftUpperArm},
    {"leftforearm",   HumanoidBones::LeftLowerArm},
    {"lefthand",      HumanoidBones::LeftHand},
    {"rightarm",      HumanoidBones::RightUpperArm},
    {"rightforearm",  HumanoidBones::RightLowerArm},
    {"righthand",     HumanoidBones::RightHand},
    {"leftupleg",     HumanoidBones::LeftUpperLeg},
    {"leftleg",       HumanoidBones::LeftLowerLeg},
    {"leftfoot",      HumanoidBones::LeftFoot},
    {"lefttoebase",   HumanoidBones::LeftToes},
    {"rightupleg",    HumanoidBones::RightUpperLeg},
    {"rightleg",      HumanoidBones::RightLowerLeg},
    {"rightfoot",     HumanoidBones::RightFoot},
    {"righttoebase",  HumanoidBones::RightToes},
};

std::string SkeletalRetargeter::map_bone_name(const std::string& raw_name) {
    const auto it = kBoneMap.find(to_lower(raw_name));
    return it != kBoneMap.end() ? it->second : raw_name;
}

void SkeletalRetargeter::retarget_to_humanoid(ir::IRSkeletonData& ir_skeleton) {
    // Naming only — see the header for what full retargeting still requires.
    for (auto& bone : ir_skeleton.bones) {
        bone.name = map_bone_name(bone.name);
    }
}

} // namespace quebratsk::converters
