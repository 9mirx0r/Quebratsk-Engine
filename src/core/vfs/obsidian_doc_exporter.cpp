#include "obsidian_doc_exporter.h"
#include <sstream>

namespace quebratsk::vfs {

using namespace godot;

void ObsidianDocExporter::_bind_methods() {
    ClassDB::bind_static_method("ObsidianDocExporter", D_METHOD("generate_vault_markdown_doc", "vfs", "root_node"), &ObsidianDocExporter::generate_vault_markdown_doc);
}

String ObsidianDocExporter::generate_vault_markdown_doc(VFSManager* vfs, Node* root_node) {
    std::stringstream ss;

    ss << "# Quebratsk Engine State Documentation\n\n";
    ss << "## Active VFS Mounts\n";

    if (vfs) {
        PackedStringArray files = vfs->list_files("");
        ss << "- Total Mounted VFS Files: " << files.size() << "\n\n";
    } else {
        ss << "_VFS Manager not initialized._\n\n";
    }

    ss << "## Active Scene Hierarchy\n";
    if (root_node) {
        ss << "- Root Node: `" << String(root_node->get_name()).utf8().get_data() << "`\n";
        ss << "- Child Count: " << root_node->get_child_count() << "\n";
    } else {
        ss << "_No root node provided._\n";
    }

    return String(ss.str().c_str());
}

} // namespace quebratsk::vfs
