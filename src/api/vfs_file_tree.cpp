#include "vfs_file_tree.h"

#include <godot_cpp/core/class_db.hpp>

namespace quebratsk::api {

using namespace godot;

void VFSFileTree::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_mounted_file_tree", "vfs"), &VFSFileTree::get_mounted_file_tree);
}

Array VFSFileTree::get_mounted_file_tree(vfs::VFSManager* vfs) {
    Array tree;
    if (!vfs) return tree;

    // Build structured file tree items for Editor Dock
    Dictionary root_node;
    root_node["name"] = "vfs://";
    root_node["type"] = "directory";
    tree.append(root_node);

    return tree;
}

} // namespace quebratsk::api
