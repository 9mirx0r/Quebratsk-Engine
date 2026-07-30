#include "occlusion_generator.h"
#include <algorithm>

namespace quebratsk::vfs {

using namespace godot;

std::future<OcclusionResult> OcclusionGenerator::compute_bounds_async(const std::vector<Vector3>& vertices) {
    return std::async(std::launch::async, [vertices]() -> OcclusionResult {
        if (vertices.empty()) return {{}, {}};

        Vector3 min_ext = vertices[0];
        Vector3 max_ext = vertices[0];

        for (const auto& v : vertices) {
            min_ext.x = std::min(min_ext.x, v.x);
            min_ext.y = std::min(min_ext.y, v.y);
            min_ext.z = std::min(min_ext.z, v.z);

            max_ext.x = std::max(max_ext.x, v.x);
            max_ext.y = std::max(max_ext.y, v.y);
            max_ext.z = std::max(max_ext.z, v.z);
        }

        // Pure data — no Godot objects created here
        return { max_ext - min_ext, (min_ext + max_ext) * 0.5f };
    });
}

OccluderInstance3D* OcclusionGenerator::create_from_result(const OcclusionResult& result) {
    // MUST be called from main thread only
    Ref<BoxOccluder3D> occluder;
    occluder.instantiate();
    occluder->set_size(result.size);

    OccluderInstance3D* instance = memnew(OccluderInstance3D);
    instance->set_occluder(occluder);
    instance->set_position(result.center);
    return instance;
}

} // namespace quebratsk::vfs
