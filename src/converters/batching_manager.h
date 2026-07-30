#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <unordered_map>
#include <vector>
#include <string>

namespace quebratsk::converters {

class BatchingManager : public godot::Object {
    GDCLASS(BatchingManager, godot::Object)

protected:
    static void _bind_methods();

public:
    BatchingManager() = default;
    ~BatchingManager() override = default;

    /// Registers a request to spawn a mesh at a specific transform.
    void register_instance(const godot::String& vfs_path, const godot::Transform3D& transform, const godot::Ref<godot::Mesh>& mesh_ref);

    /// Flushes all registered instances into the provided parent node.
    void flush(godot::Node* parent_node);
    
    /// Clears the current batch registry
    void clear();

private:
    struct MeshEntry {
        godot::Ref<godot::Mesh> mesh;
        std::vector<godot::Transform3D> instances;
    };

    std::unordered_map<std::string, MeshEntry> _batch_registry;
};

} // namespace quebratsk::converters
