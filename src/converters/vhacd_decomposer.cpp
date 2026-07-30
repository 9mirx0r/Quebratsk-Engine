#include "vhacd_decomposer.h"
#include "collision_converter.h"

namespace quebratsk::converters {

using namespace godot;

ir::IRCollisionData VHACDDecomposer::decompose(
    const ir::IRMeshData& ir_mesh,
    const VHACDParameters& params
) {
    // WARNING: this is NOT V-HACD. It merges every vertex into a single convex hull,
    // which is the opposite of an approximate convex decomposition — concave geometry
    // loses its cavities entirely. `params` is ignored. Treat this as a placeholder
    // that produces one coarse hull, not as a decomposition.
    ir::IRCollisionData col_data;
    col_data.name = ir_mesh.name + "_Collision";

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
