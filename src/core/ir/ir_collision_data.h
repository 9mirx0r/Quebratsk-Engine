#pragma once

#include <vector>

#include <godot_cpp/variant/vector3.hpp>

namespace quebratsk::ir {

struct IRConvexHull {
    std::vector<godot::Vector3> vertices;
};

struct IRCollisionData {
    SourceEngine source_engine = SourceEngine::GoldSrc;
    std::vector<IRConvexHull> convex_hulls;

    struct ClipPlane {
        godot::Vector3 normal;
        float distance = 0.0f;
    };
    std::vector<ClipPlane> clip_planes;
};

} // namespace quebratsk::ir
