#include "vfs_drop_handler.h"
#include <godot_cpp/classes/engine.hpp>
#include <filesystem>

namespace quebratsk::api {

using namespace godot;

void VFSDropHandler::_bind_methods() {
    ClassDB::bind_static_method("VFSDropHandler", D_METHOD("handle_dropped_files", "files", "vfs"), &VFSDropHandler::handle_dropped_files);
}

bool VFSDropHandler::handle_dropped_files(const PackedStringArray& files, quebratsk::vfs::VFSManager* vfs) {
    if (!vfs || files.is_empty()) return false;

    bool any_mounted = false;

    for (int i = 0; i < files.size(); ++i) {
        String file_path = files[i];
        std::filesystem::path p(file_path.utf8().get_data());
        std::string ext = p.extension().string();

        // Convert extension to lowercase
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".wad" || ext == ".gma" || ext == ".pbo" || ext == ".vpk" || ext == ".pak" || ext == ".bundle") {
            std::string mount_point = p.stem().string();
            std::string path_str = p.string();

            if (vfs->mount_container(mount_point, path_str)) {
                any_mounted = true;
            }
        }
    }

    return any_mounted;
}

} // namespace quebratsk::api
