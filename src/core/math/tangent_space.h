#pragma once

namespace quebratsk::math {

struct Tangent4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f; // Bitangent sign (-1 or 1)
};

/// Transform 4-component tangent from Source Z-Up Left-Handed to Godot Y-Up Right-Handed
[[nodiscard]] inline Tangent4 transform_tangent_source_to_godot(const Tangent4& t) {
    return {
        t.x,
        t.z,
        -t.y,
        -t.w // Handedness flip negates bitangent sign
    };
}

} // namespace quebratsk::math
