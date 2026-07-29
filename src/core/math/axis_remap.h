#pragma once

#include "unit_scale.h"

#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace quebratsk::math {

/// Remap Z-Up Left-Handed coordinates to Godot Y-Up Right-Handed coordinates
/// Source (X, Y, Z) -> Godot (X * scale, Z * scale, -Y * scale)
[[nodiscard]] inline godot::Vector3 source_to_godot(const godot::Vector3& v, double scale = kHammerUnitsToMeters) {
    return godot::Vector3(
        static_cast<float>(v.x * scale),
        static_cast<float>(v.z * scale),
        static_cast<float>(-v.y * scale)
    );
}

/// Remap Real Virtuality Z-Up metric coordinates to Godot Y-Up
[[nodiscard]] inline godot::Vector3 rv_to_godot(const godot::Vector3& v) {
    return godot::Vector3(v.x, v.z, -v.y);
}

/// Remap normal vector (preserves unit length, no scale applied)
[[nodiscard]] inline godot::Vector3 transform_normal_zup_to_yup(const godot::Vector3& n) {
    return godot::Vector3(n.x, n.z, -n.y).normalized();
}

/// Remap quaternion orientation (Z-Up to Y-Up)
[[nodiscard]] inline godot::Quaternion source_quat_to_godot(const godot::Quaternion& q) {
    return godot::Quaternion(q.x, q.z, -q.y, q.w);
}

/// Remap 4x4 transform matrix from Source to Godot
[[nodiscard]] godot::Transform3D source_transform_to_godot(const godot::Transform3D& src);

} // namespace quebratsk::math
