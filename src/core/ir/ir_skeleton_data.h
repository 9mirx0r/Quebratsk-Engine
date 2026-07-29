#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

} // namespace quebratsk::ir
