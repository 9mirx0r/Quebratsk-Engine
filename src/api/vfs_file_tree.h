#pragma once

#include "../core/vfs/vfs_manager.h"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace quebratsk::api {

class VFSFileTree : public godot::Node {
    GDCLASS(VFSFileTree, godot::Node);

protected:
    static void _bind_methods();

public:
    VFSFileTree() = default;
    ~VFSFileTree() override = default;

    /// Get structured tree representation of all mounted VFS archives for Editor Dock UI
    godot::Array get_mounted_file_tree(vfs::VFSManager* vfs);
};

} // namespace quebratsk::api
