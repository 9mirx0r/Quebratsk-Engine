#pragma once

#include <string>
#include <vector>

#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace quebratsk::ir {

struct IRAnimKeyframe {
    float time = 0.0f; // Seconds
    godot::Vector3 position;
    godot::Quaternion rotation;
    godot::Vector3 scale = godot::Vector3(1, 1, 1);
};

struct IRAnimBoneTrack {
    std::string bone_name;
    std::vector<IRAnimKeyframe> keyframes;
};

struct IRAnimationData {
    SourceEngine source_engine = SourceEngine::GoldSrc;
    std::string name;
    float duration = 0.0f; // Total duration in seconds
    float fps = 30.0f;
    bool looping = false;
    std::vector<IRAnimBoneTrack> bone_tracks;
};

} // namespace quebratsk::ir
