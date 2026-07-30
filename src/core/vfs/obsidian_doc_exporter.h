#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/classes/node.hpp>
#include "../core/vfs/vfs_manager.h"

namespace quebratsk::vfs {

class ObsidianDocExporter : public godot::Object {
    GDCLASS(ObsidianDocExporter, godot::Object)

protected:
    static void _bind_methods();

public:
    ObsidianDocExporter() = default;
    ~ObsidianDocExporter() = default;

    /// Generates a Markdown document detailing active VFS mounts and scene node tree state,
    /// ready for Obsidian Vault sync.
    static godot::String generate_vault_markdown_doc(
        quebratsk::vfs::VFSManager* vfs,
        godot::Node* root_node
    );
};

} // namespace quebratsk::vfs
