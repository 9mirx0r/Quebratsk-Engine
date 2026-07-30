#include "batching_manager.h"

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>

namespace quebratsk::converters {

using namespace godot;

void BatchingManager::_bind_methods() {
    ClassDB::bind_method(D_METHOD("register_instance", "vfs_path", "transform", "mesh_ref"), &BatchingManager::register_instance);
    ClassDB::bind_method(D_METHOD("flush", "parent_node"), &BatchingManager::flush);
    ClassDB::bind_method(D_METHOD("clear"), &BatchingManager::clear);
}

void BatchingManager::register_instance(const String& vfs_path, const Transform3D& transform, const Ref<Mesh>& mesh_ref) {
    if (mesh_ref.is_null()) return;

    std::string key = vfs_path.utf8().get_data();
    auto& entry = _batch_registry[key];
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
            MeshInstance3D* mi = memnew(MeshInstance3D);
            mi->set_mesh(entry.mesh);
            mi->set_transform(entry.instances[0]);
            parent_node->add_child(mi);
            if (parent_node->get_tree()) {
                mi->set_owner(parent_node->get_tree()->get_edited_scene_root());
            }
        } else {
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
            if (parent_node->get_tree()) {
                mmi->set_owner(parent_node->get_tree()->get_edited_scene_root());
            }
        }
    }

    clear();
}

void BatchingManager::clear() {
    _batch_registry.clear();
}

} // namespace quebratsk::converters
