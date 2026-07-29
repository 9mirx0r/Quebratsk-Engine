#pragma once

#include "unified_asset_importer.h"

#include <godot_cpp/classes/callable.hpp>
#include <godot_cpp/classes/node.hpp>

#include <future>
#include <thread>

namespace quebratsk::api {

class AsyncAssetImporter : public godot::Node {
    GDCLASS(AsyncAssetImporter, godot::Node);

protected:
    static void _bind_methods();

public:
    AsyncAssetImporter() = default;
    ~AsyncAssetImporter() override = default;

    /// Load 3D mesh asynchronously on a background thread and call callback with Result ArrayMesh
    void load_mesh_async(UnifiedAssetImporter* importer, const godot::String& vfs_uri, const godot::Callable& callback);
};

} // namespace quebratsk::api
