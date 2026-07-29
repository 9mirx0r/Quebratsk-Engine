#include "batching_manager.h"

#include <godot_cpp/classes/mesh_instance3d.hpp>

namespace quebratsk::converters {

using namespace godot;

void BatchingManager::register_instance(const std::string& vfs_path, const Transform3D& transform, const Ref<Mesh>& mesh_ref) {
    if (mesh_ref.is_null()) return;

    auto& entry = _batch_registry[vfs_path];
    if (entry.mesh.is_null()) {
        entry.mesh = mesh_ref;
    }
    entry.instances.push_back(transform);
}

void BatchingManager::flush(Node* parent_node) {
    if (!parent_node) return;

    for (const auto& [path, entry] : _batch_registry) {
        size_t count = entry.instances.size();
        if (count == 0) continue;

        if (count == 1) {
            // Spawn a normal MeshInstance3D
            MeshInstance3D* mi = memnew(MeshInstance3D);
            mi->set_mesh(entry.mesh);
            mi->set_transform(entry.instances[0]);
            parent_node->add_child(mi);
            mi->set_owner(parent_node->get_tree() ? parent_node->get_tree()->get_edited_scene_root() : parent_node);
        } else {
            // Spawn a MultiMeshInstance3D to batch draw calls
            Ref<MultiMesh> mm;
            mm.instantiate();
            mm->set_transform_format(MultiMesh::TRANSFORM_3D);
            mm->set_mesh(entry.mesh);
            mm->set_instance_count(static_cast<int>(count));

            for (size_t i = 0; i < count; ++i) {
                mm->set_instance_transform(static_cast<int>(i), entry.instances[i]);
            }

            MultiMeshInstance3D* mmi = memnew(MultiMeshInstance3D);
            mmi->set_multimesh(mm);
            parent_node->add_child(mmi);
            mmi->set_owner(parent_node->get_tree() ? parent_node->get_tree()->get_edited_scene_root() : parent_node);
        }
    }

    clear();
}

void BatchingManager::clear() {
    _batch_registry.clear();
}

} // namespace quebratsk::converters
