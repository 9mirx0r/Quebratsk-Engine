#include "batching_manager.h"

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>

namespace quebratsk::converters {

using namespace godot;

void BatchingManager::_bind_methods() {
    ClassDB::bind_method(D_METHOD("register_instance", "vfs_path", "transform", "mesh_ref"), &BatchingManager::register_instance);
    ClassDB::bind_method(D_METHOD("flush", "parent_node"), &BatchingManager::flush);
    ClassDB::bind_method(D_METHOD("clear"), &BatchingManager::clear);
}

void BatchingManager::register_instance(const String& vfs_path, const Transform3D& transform, const Ref<Mesh>& mesh_ref) {
    if (mesh_ref.is_null()) return;

    std::lock_guard<std::mutex> lock(_mutex);
    std::string key = vfs_path.utf8().get_data();
    auto& entry = _batch_registry[key];
    if (entry.mesh.is_null()) {
        entry.mesh = mesh_ref;
    }
    entry.instances.push_back(transform);
}

void BatchingManager::flush(Node* parent_node) {
    if (!parent_node) return;

    // memnew(), add_child() and set_owner() are main-thread-only operations.
    OS* os = OS::get_singleton();
    ERR_FAIL_COND_MSG(os && os->get_thread_caller_id() != os->get_main_thread_id(),
                      "BatchingManager::flush() must be called from the main thread.");

    // Move the registry out under the lock, then touch the scene tree with the lock
    // released. Holding _mutex across Godot calls risked deadlock: any callback that
    // re-entered register_instance() during add_child() would block on a non-recursive
    // mutex already owned by this thread.
    std::unordered_map<std::string, MeshEntry> local;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        local.swap(_batch_registry);
    }

    Node* owner = parent_node->get_tree()
                ? parent_node->get_tree()->get_edited_scene_root()
                : nullptr;

    for (const auto& [path, entry] : local) {
        const size_t count = entry.instances.size();
        if (count == 0 || entry.mesh.is_null()) continue;

        if (count == 1) {
            MeshInstance3D* mi = memnew(MeshInstance3D);
            mi->set_mesh(entry.mesh);
            mi->set_transform(entry.instances[0]);
            parent_node->add_child(mi);
            if (owner) mi->set_owner(owner);
        } else {
            Ref<MultiMesh> mm;
            mm.instantiate();
            mm->set_transform_format(MultiMesh::TRANSFORM_3D);
            mm->set_mesh(entry.mesh);
            mm->set_instance_count(static_cast<int>(count));

            // One set_buffer() upload instead of N set_instance_transform() calls,
            // each of which crosses the GDExtension boundary and re-validates the RID.
            PackedFloat32Array buf;
            buf.resize(static_cast<int64_t>(count) * 12);
            float* w = buf.ptrw();
            for (size_t i = 0; i < count; ++i) {
                const Transform3D& t = entry.instances[i];
                float* d = w + i * 12;
                d[0] = t.basis[0][0]; d[1] = t.basis[0][1]; d[2] = t.basis[0][2]; d[3]  = t.origin.x;
                d[4] = t.basis[1][0]; d[5] = t.basis[1][1]; d[6] = t.basis[1][2]; d[7]  = t.origin.y;
                d[8] = t.basis[2][0]; d[9] = t.basis[2][1]; d[10] = t.basis[2][2]; d[11] = t.origin.z;
            }
            mm->set_buffer(buf);

            MultiMeshInstance3D* mmi = memnew(MultiMeshInstance3D);
            mmi->set_multimesh(mm);
            parent_node->add_child(mmi);
            if (owner) mmi->set_owner(owner);
        }
    }
}

void BatchingManager::clear() {
    std::lock_guard<std::mutex> lock(_mutex);
    _batch_registry.clear();
}

} // namespace quebratsk::converters
