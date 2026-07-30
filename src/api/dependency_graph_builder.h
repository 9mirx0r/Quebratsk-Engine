#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include "../core/vfs/vfs_manager.h"

namespace quebratsk::api {

class DependencyGraphBuilder : public godot::Object {
    GDCLASS(DependencyGraphBuilder, godot::Object)

protected:
    static void _bind_methods();

public:
    DependencyGraphBuilder() = default;
    ~DependencyGraphBuilder() = default;

    /// Scans a VFS asset (BSP, MDL, P3D) and builds a Dictionary node tree representing
    /// dependencies (Mesh -> Materials -> Textures) and flags missing assets in red.
    static godot::Dictionary build_dependency_graph(
        const godot::String& asset_vfs_path,
        quebratsk::vfs::VFSManager* vfs
    );
};

} // namespace quebratsk::api
