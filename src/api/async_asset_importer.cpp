#include "async_asset_importer.h"
#include "../converters/texture_loader.h"

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

void AsyncAssetImporter::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load_mesh_async", "importer", "vfs_uri", "callback"),
                         &AsyncAssetImporter::load_mesh_async);
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

    // Reap threads that already finished so the vector does not grow without bound.
    std::erase_if(_workers, [](const std::jthread& t) { return !t.joinable(); });

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

} // namespace quebratsk::api
