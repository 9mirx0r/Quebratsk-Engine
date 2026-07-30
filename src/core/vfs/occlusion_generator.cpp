#include "occlusion_generator.h"
#include <algorithm>
#include <thread>

namespace quebratsk::vfs {

using namespace godot;

std::future<Ref<BoxOccluder3D>> OcclusionGenerator::generate_async(const std::vector<Vector3>& vertices) {
    return std::async(std::launch::async, [vertices]() -> Ref<BoxOccluder3D> {
        if (vertices.empty()) return Ref<BoxOccluder3D>();

        Vector3 min_extents = vertices[0];
        Vector3 max_extents = vertices[0];

        for (const auto& v : vertices) {
            min_extents.x = std::min(min_extents.x, v.x);
            min_extents.y = std::min(min_extents.y, v.y);
            min_extents.z = std::min(min_extents.z, v.z);
            
            max_extents.x = std::max(max_extents.x, v.x);
            max_extents.y = std::max(max_extents.y, v.y);
            max_extents.z = std::max(max_extents.z, v.z);
        }

        Vector3 size = max_extents - min_extents;
        
        // We can't instantiate Godot resources safely from a background thread
        // wait, actually Ref<BoxOccluder3D> instantiation might be okay if it's just a resource,
        // but to be perfectly thread-safe in Godot 4, we should create it here if Godot allows,
        // or just return the size/extents. Let's create it.
        Ref<BoxOccluder3D> occluder;
        occluder.instantiate();
        occluder->set_size(size);
        
        return occluder;
    });
}

OccluderInstance3D* OcclusionGenerator::create_instance(const Ref<BoxOccluder3D>& occluder) {
    if (occluder.is_null()) return nullptr;
    
    OccluderInstance3D* instance = memnew(OccluderInstance3D);
    instance->set_occluder(occluder);
    return instance;
}

} // namespace quebratsk::vfs
