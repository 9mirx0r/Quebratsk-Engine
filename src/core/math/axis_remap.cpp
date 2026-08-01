#include "axis_remap.h"

namespace quebratsk::math {

godot::Transform3D source_transform_to_godot(const godot::Transform3D& src) {
    // M B Mt, written out. M sends (x, y, z) to (-y, z, -x), so a row or column of the source
    // basis is permuted the same way, and the columns are reordered to match where each source
    // axis ended up: Godot's X is Valve's -Y, Godot's Y is Valve's Z, Godot's Z is Valve's -X.
    auto remap = [](const godot::Vector3& v) {
        return godot::Vector3(-v.y, v.z, -v.x);
    };
    const godot::Vector3 col_x(src.basis[0][0], src.basis[1][0], src.basis[2][0]);
    const godot::Vector3 col_y(src.basis[0][1], src.basis[1][1], src.basis[2][1]);
    const godot::Vector3 col_z(src.basis[0][2], src.basis[1][2], src.basis[2][2]);

    godot::Basis new_basis;
    new_basis.set_column(0, -remap(col_y));
    new_basis.set_column(1, remap(col_z));
    new_basis.set_column(2, -remap(col_x));

    godot::Vector3 new_origin = source_to_godot(src.origin);
    return godot::Transform3D(new_basis, new_origin);
}

} // namespace quebratsk::math
