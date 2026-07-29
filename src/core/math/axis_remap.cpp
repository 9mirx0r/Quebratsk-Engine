#include "axis_remap.h"

namespace quebratsk::math {

godot::Transform3D source_transform_to_godot(const godot::Transform3D& src) {
    godot::Basis new_basis;
    new_basis.set_column(0, godot::Vector3(src.basis[0][0], src.basis[2][0], -src.basis[1][0]));
    new_basis.set_column(1, godot::Vector3(src.basis[0][2], src.basis[2][2], -src.basis[1][2]));
    new_basis.set_column(2, godot::Vector3(-src.basis[0][1], -src.basis[2][1], src.basis[1][1]));

    godot::Vector3 new_origin = source_to_godot(src.origin);
    return godot::Transform3D(new_basis, new_origin);
}

} // namespace quebratsk::math
