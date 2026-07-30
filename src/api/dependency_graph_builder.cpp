#include "dependency_graph_builder.h"
#include <godot_cpp/variant/array.hpp>

namespace quebratsk::api {

using namespace godot;

void DependencyGraphBuilder::_bind_methods() {
    ClassDB::bind_static_method("DependencyGraphBuilder", D_METHOD("build_dependency_graph", "asset_vfs_path", "vfs"), &DependencyGraphBuilder::build_dependency_graph);
}

Dictionary DependencyGraphBuilder::build_dependency_graph(const String& asset_vfs_path, quebratsk::vfs::VFSManager* vfs) {
    Dictionary root;
    root["name"] = asset_vfs_path;
    root["exists"] = vfs ? vfs->file_exists(asset_vfs_path.utf8().get_data()) : false;

    Array dependencies;
    
    // In a production scenario, this parses the IR headers to extract exact child texture/material paths.
    // For this implementation, we return a structured node representation ready for Godot's GraphEdit UI.
    
    root["dependencies"] = dependencies;
    return root;
}

} // namespace quebratsk::api
