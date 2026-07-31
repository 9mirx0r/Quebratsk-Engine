#include "async_asset_importer.h"
#include "../converters/texture_loader.h"
#include "../converters/skeleton_converter.h"
#include "../core/config/quebratsk_settings.h"
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cctype>
#include <utility>

namespace quebratsk::api {

using namespace godot;

AsyncAssetImporter::~AsyncAssetImporter() {
    // std::jthread requests stop and joins on destruction, but do it explicitly so
    // no worker can still be running when the GDExtension library is unloaded.
    for (auto& worker : _workers) {
        worker.request_stop();
        if (worker.joinable()) {
            worker.join();
        }
    }
    _workers.clear();

    std::lock_guard<std::mutex> lock(_pending_mutex);
    _pending.clear();
}

/// Drop threads that have finished, and block if too many are still running.
///
/// Every call used to spawn a std::jthread unconditionally, so importing a folder of 200
/// models from a loop created 200 threads at once — each holding a decoded copy of its
/// asset. This honours quebratsk/performance/max_background_threads, which until now was
/// a Project Setting nothing read.
void AsyncAssetImporter::_reap_finished_workers() {
    std::erase_if(_workers, [](const std::jthread& t) { return !t.joinable(); });

    const size_t cap = static_cast<size_t>(config::QuebratskSettings::max_background_threads());
    while (_workers.size() >= cap) {
        // Oldest first: joining it is what bounds memory, and callbacks are delivered on
        // the main thread by call_deferred() regardless of completion order.
        _workers.front().join();
        _workers.erase(_workers.begin());
        std::erase_if(_workers, [](const std::jthread& t) { return !t.joinable(); });
    }
}

void AsyncAssetImporter::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load_mesh_async", "importer", "vfs_uri", "callback"),
                         &AsyncAssetImporter::load_mesh_async);
    ClassDB::bind_method(D_METHOD("load_model_async", "importer", "vfs_uri", "pose_name", "callback"),
                         &AsyncAssetImporter::load_model_async, DEFVAL(String()));
}

void AsyncAssetImporter::load_mesh_async(UnifiedAssetImporter* importer,
                                         const String& vfs_uri,
                                         const Callable& callback) {
    if (!importer) {
        UtilityFunctions::printerr("[QuebratskAsync] Null importer passed to load_mesh_async().");
        return;
    }
    if (!importer->get_vfs()) {
        UtilityFunctions::printerr("[QuebratskAsync] Importer has no VFSManager set.");
        return;
    }

    // Read on the main thread: VFSManager owns memory mappings whose lifetime is tied
    // to mount/unmount, and resolving companion files (a Source .mdl needs its .vvd and
    // .vtx) requires VFS lookups. The worker then operates purely on these owned buffers.
    AssetBundleBytes owned_bundle = importer->read_asset_bundle(vfs_uri);
    if (owned_bundle.empty()) {
        UtilityFunctions::printerr("[QuebratskAsync] Empty or unreadable asset: ", vfs_uri);
        return;
    }

    std::string uri_lower = vfs_uri.utf8().get_data();
    std::transform(uri_lower.begin(), uri_lower.end(), uri_lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const int64_t job_id = _next_job_id.fetch_add(1, std::memory_order_relaxed);
    const uint64_t importer_id = importer->get_instance_id();

    _reap_finished_workers();

    _workers.emplace_back(
        [this, job_id, importer_id, bundle = std::move(owned_bundle), uri_lower, callback](std::stop_token stoken) {
            if (stoken.stop_requested()) {
                return;
            }

            // Pure-data decode. No Ref<>, no memnew(), no server calls.
            ir::IRMeshData mesh_ir = UnifiedAssetImporter::parse_asset_ir(bundle, uri_lower).mesh;

            if (stoken.stop_requested()) {
                return;
            }

            {
                std::lock_guard<std::mutex> lock(_pending_mutex);
                _pending.emplace(job_id, PendingJob{std::move(mesh_ir), callback, importer_id});
            }

            // Hop back to the main thread; ArrayMesh is built there.
            callable_mp(this, &AsyncAssetImporter::_deliver_mesh).call_deferred(job_id);
        });
}

void AsyncAssetImporter::_deliver_mesh(int64_t job_id) {
    PendingJob job;
    {
        std::lock_guard<std::mutex> lock(_pending_mutex);
        auto it = _pending.find(job_id);
        if (it == _pending.end()) {
            return;
        }
        job = std::move(it->second);
        _pending.erase(it);
    }

    if (job.mesh_ir.surfaces.empty()) {
        UtilityFunctions::printerr("[QuebratskAsync] No mesh surfaces decoded for job ", job_id);
        if (job.callback.is_valid()) {
            job.callback.call(Ref<ArrayMesh>());
        }
        return;
    }

    // Main thread: safe to allocate Resources and talk to the RenderingServer.
    // Re-resolve the importer by ObjectID — it may have been freed while the worker ran.
    Ref<ArrayMesh> mesh;
    auto* importer = Object::cast_to<UnifiedAssetImporter>(ObjectDB::get_instance(job.importer_id));
    if (importer && importer->get_vfs()) {
        converters::TextureLoader loader(importer->get_vfs());
        mesh = converters::MeshConverter::convert(job.mesh_ir, &loader);
    } else {
        mesh = converters::MeshConverter::convert(job.mesh_ir);
    }

    if (job.callback.is_valid()) {
        job.callback.call(mesh);
    }
}

void AsyncAssetImporter::load_model_async(UnifiedAssetImporter* importer,
                                          const String& vfs_uri,
                                          const String& pose_name,
                                          const Callable& callback) {
    if (!importer || !importer->get_vfs()) {
        UtilityFunctions::printerr("[QuebratskAsync] Importer or VFS missing in load_model_async().");
        return;
    }

    AssetBundleBytes owned_bundle = importer->read_asset_bundle(vfs_uri);
    if (owned_bundle.empty()) {
        UtilityFunctions::printerr("[QuebratskAsync] Empty or unreadable asset: ", vfs_uri);
        return;
    }

    std::string uri_lower = vfs_uri.utf8().get_data();
    std::transform(uri_lower.begin(), uri_lower.end(), uri_lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const int64_t job_id = _next_job_id.fetch_add(1, std::memory_order_relaxed);
    const uint64_t importer_id = importer->get_instance_id();
    std::string pose_str = pose_name.utf8().get_data();

    std::erase_if(_workers, [](const std::jthread& t) { return !t.joinable(); });

    _workers.emplace_back(
        [this, job_id, importer_id, bundle = std::move(owned_bundle), uri_lower, pose_str, callback](std::stop_token stoken) {
            if (stoken.stop_requested()) return;

            ParsedAssetIR parsed = UnifiedAssetImporter::parse_asset_ir(bundle, uri_lower);
            if (stoken.stop_requested()) return;

            {
                std::lock_guard<std::mutex> lock(_pending_mutex);
                _pending_models.emplace(job_id, PendingModelJob{std::move(parsed), pose_str, callback, importer_id});
            }

            callable_mp(this, &AsyncAssetImporter::_deliver_model).call_deferred(job_id);
        });
}

void AsyncAssetImporter::_deliver_model(int64_t job_id) {
    PendingModelJob job;
    {
        std::lock_guard<std::mutex> lock(_pending_mutex);
        auto it = _pending_models.find(job_id);
        if (it == _pending_models.end()) return;
        job = std::move(it->second);
        _pending_models.erase(it);
    }

    auto* importer = Object::cast_to<UnifiedAssetImporter>(ObjectDB::get_instance(job.importer_id));

    // Build from the IR the worker already produced.
    //
    // This used to call load_model(parsed_ir.mesh.name, ...), which was wrong twice over:
    // mesh.name is the model's *internal* name from its header ("player/lowpoly/x.mdl"),
    // not a vfs:// URI, so the lookup always missed and the callback always received
    // null; and going back through load_model() would have re-read and re-parsed the
    // whole asset on the main thread, discarding the background work entirely.
    Node3D* model_node = nullptr;
    if (importer) {
        model_node = importer->build_model_node(job.parsed_ir, String(job.pose_name.c_str()));
    }

    if (job.callback.is_valid()) {
        job.callback.call(model_node);
    }
}

} // namespace quebratsk::api
