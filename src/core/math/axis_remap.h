#pragma once

#include "unit_scale.h"

#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace quebratsk::math {

/// Remap Z-Up Valve coordinates to Godot Y-Up coordinates.
/// Source (X, Y, Z) -> Godot (-Y * scale, Z * scale, -X * scale)
///
/// Valve puts +X forward, +Y left and +Z up. Godot puts +X right, +Y up and -Z forward.
/// Lining those up is what the mapping says: forward becomes forward, left becomes left,
/// up becomes up.
///
///     M = [ 0 -1  0 ]
///         [ 0  0  1 ]      det(M) = +1
///         [-1  0  0 ]
///
/// A positive determinant means this transform PRESERVES orientation, so triangle winding
/// is already correct in Godot. Do NOT pair it with invert_winding_order(): doing so flips
/// every face and the geometry renders inside-out under backface culling. The same reasoning
/// is why source_quat_to_godot() only permutes the quaternion's vector part.
///
/// This used to be (X, Z, -Y), which is also orthogonal and also preserves winding, and was
/// wrong in a way nothing measurable caught: it sends Valve's forward to Godot's RIGHT, so
/// every imported model faced ninety degrees away from where Godot considers front. Geometry
/// and entities agreed with each other, so maps looked correct and nothing reported an error.
/// It only showed when a character was asked to face something. An NPC aiming at the player
/// with look_at() points its -Z at them and stood there sideways, and a first-person camera
/// offset along the body's facing walked into the model's own shoulder. Screenshots found it;
/// four verification harnesses measuring counts and distances did not, because a rotation
/// changes neither.
[[nodiscard]] inline godot::Vector3 source_to_godot(const godot::Vector3& v, double scale = kHammerUnitsToMeters) {
    return godot::Vector3(
        static_cast<float>(-v.y * scale),
        static_cast<float>(v.z * scale),
        static_cast<float>(-v.x * scale)
    );
}

/// Remap Real Virtuality Z-Up metric coordinates to Godot Y-Up
[[nodiscard]] inline godot::Vector3 rv_to_godot(const godot::Vector3& v) {
    return godot::Vector3(v.x, v.z, -v.y);
}

/// Remap normal vector (preserves unit length, no scale applied)
[[nodiscard]] inline godot::Vector3 transform_normal_zup_to_yup(const godot::Vector3& n) {
    return godot::Vector3(-n.y, n.z, -n.x).normalized();
}

/// Remap quaternion orientation (Z-Up to Y-Up).
///
/// For a change of basis M that is a proper rotation, a rotation R becomes M R Mt, and the
/// quaternion of that is simply M applied to the vector part with w untouched. That is why
/// this permutes three components and leaves the fourth alone, and why it has to permute
/// them the same way source_to_godot() does: the two describe one basis change.
[[nodiscard]] inline godot::Quaternion source_quat_to_godot(const godot::Quaternion& q) {
    return godot::Quaternion(-q.y, q.z, -q.x, q.w);
}

/// Remap 4x4 transform matrix from Source to Godot
[[nodiscard]] godot::Transform3D source_transform_to_godot(const godot::Transform3D& src);

} // namespace quebratsk::math
