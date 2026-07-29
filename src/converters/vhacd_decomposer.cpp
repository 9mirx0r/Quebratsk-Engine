#include "vhacd_decomposer.h"
#include "collision_converter.h"

namespace quebratsk::converters {

using namespace godot;

ir::IRCollisionData VHACDDecomposer::decompose(
    const ir::IRMeshData& ir_mesh,
    const VHACDParameters& params
) {
    ir::IRCollisionData col_data;
    col_data.name = ir_mesh.name + "_Collision";

    // Build initial convex hull from mesh vertices
    ir::IRConvexHull hull;
    hull.name = "Hull_0";

    for (const auto& surf : ir_mesh.surfaces) {
        hull.vertices.insert(hull.vertices.end(), surf.positions.begin(), surf.positions.end());
    }

    if (!hull.vertices.empty()) {
        col_data.convex_hulls.push_back(std::move(hull));
    }

    return col_data;
}

std::vector<Ref<ConvexPolygonShape3D>> VHACDDecomposer::generate_godot_shapes(
    const ir::IRCollisionData& collision_data
) {
    std::vector<Ref<ConvexPolygonShape3D>> shapes;
    for (const auto& hull : collision_data.convex_hulls) {
        ir::IRCollisionData single_col;
        single_col.convex_hulls.push_back(hull);
        shapes.push_back(CollisionConverter::convert(single_col));
    }
    return shapes;
}

} // namespace quebratsk::converters
