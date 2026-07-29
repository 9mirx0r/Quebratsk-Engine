#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/transform3d.hpp>
#include <godot_cpp/classes/ref.hpp>

#include <unordered_map>
#include <vector>
#include <string>

namespace quebratsk::converters {

class BatchingManager {
public:
    BatchingManager() = default;
    ~BatchingManager() = default;

    /// Registers a request to spawn a mesh at a specific transform.
    /// Does not immediately instantiate it in the scene tree.
    void register_instance(const std::string& vfs_path, const godot::Transform3D& transform, const godot::Ref<godot::Mesh>& mesh_ref);

    /// Flushes all registered instances into the provided parent node.
    /// - If a mesh is requested exactly once, creates a MeshInstance3D.
    /// - If requested multiple times, groups them into a single MultiMeshInstance3D (Auto-Batching).
    void flush(godot::Node* parent_node);
    
    /// Clears the current batch registry
    void clear();

private:
    struct MeshEntry {
        godot::Ref<godot::Mesh> mesh;
        std::vector<godot::Transform3D> instances;
    };

    // Maps VFS path to a list of transforms and the mesh reference
    std::unordered_map<std::string, MeshEntry> _batch_registry;
};

} // namespace quebratsk::converters
