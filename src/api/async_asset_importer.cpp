#include "async_asset_importer.h"

#include <godot_cpp/core/class_db.hpp>

namespace quebratsk::api {

using namespace godot;

void AsyncAssetImporter::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load_mesh_async", "importer", "vfs_uri", "callback"), &AsyncAssetImporter::load_mesh_async);
}

void AsyncAssetImporter::load_mesh_async(UnifiedAssetImporter* importer, const String& vfs_uri, const Callable& callback) {
    if (!importer) return;

    std::thread([importer, vfs_uri, callback]() {
        Ref<ArrayMesh> mesh = importer->load_mesh(vfs_uri);
        callback.call_deferred(mesh);
    }).detach();
}

} // namespace quebratsk::api
