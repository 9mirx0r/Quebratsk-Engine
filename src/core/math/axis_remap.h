#pragma once

#include "unit_scale.h"

#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace quebratsk::math {

/// Remap Z-Up coordinates to Godot Y-Up coordinates.
/// Source (X, Y, Z) -> Godot (X * scale, Z * scale, -Y * scale)
///
/// INVARIANT: the underlying basis change is
///     M = [ 1  0  0 ]
///         [ 0  0  1 ]      det(M) = +1
///         [ 0 -1  0 ]
/// A positive determinant means this transform PRESERVES orientation, so triangle
/// winding is already correct in Godot. Do NOT pair it with invert_winding_order():
/// doing so flips every face and the geometry renders inside-out under backface
/// culling. The same reasoning is why source_quat_to_godot() only permutes the
/// quaternion's vector part.
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
