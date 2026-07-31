#pragma once

#include "unified_asset_importer.h"
#include "../core/ir/ir_mesh_data.h"

#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/classes/node.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace quebratsk::api {

class AsyncAssetImporter : public godot::Node {
    GDCLASS(AsyncAssetImporter, godot::Node);

protected:
    static void _bind_methods();

public:
    AsyncAssetImporter() = default;
    ~AsyncAssetImporter() override;

    /// Decode a mesh on a background thread, then hand the finished ArrayMesh to
    /// `callback` on the main thread.
    ///
    /// Threading contract: VFS reads and every Godot Object/Resource allocation stay
    /// on the main thread. The worker only touches the engine-agnostic IR, which is
    /// plain std::vector/std::string. See parse_mesh_ir().
    void load_mesh_async(UnifiedAssetImporter* importer,
                         const godot::String& vfs_uri,
                         const godot::Callable& callback);

    /// Decode a complete model (mesh + skeleton + poses) on a background thread, then
    /// construct the Node3D scene graph and invoke `callback(model_node: Node3D)` on the main thread.
    void load_model_async(UnifiedAssetImporter* importer,
                          const godot::String& vfs_uri,
                          const godot::String& pose_name,
                          const godot::Callable& callback);

private:
    struct PendingJob {
        ir::IRMeshData mesh_ir;
        godot::Callable callback;
        uint64_t importer_id = 0;
    };

    struct PendingModelJob {
        ParsedAssetIR parsed_ir;
        std::string pose_name;
        godot::Callable callback;
        uint64_t importer_id = 0;
    };

    void _deliver_mesh(int64_t job_id);
    void _deliver_model(int64_t job_id);

    /// Reap finished workers, and block until the running count is under the configured
    /// cap. Main thread only: called from load_*_async() before spawning.
    void _reap_finished_workers();

    std::vector<std::jthread> _workers;
    std::mutex _pending_mutex;
    std::unordered_map<int64_t, PendingJob> _pending;
    std::unordered_map<int64_t, PendingModelJob> _pending_models;
    std::atomic<int64_t> _next_job_id{1};
};

} // namespace quebratsk::api
