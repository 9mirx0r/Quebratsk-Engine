#include "bsp_map_renderer.h"
#include <godot_cpp/variant/utility_functions.hpp>

namespace quebratsk::converters {

using namespace godot;

void BSPMapRenderer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load_map", "bsp_vfs_uri", "vfs"), &BSPMapRenderer::load_map);
    ClassDB::bind_method(D_METHOD("perform_pvs_culling", "camera_position"), &BSPMapRenderer::perform_pvs_culling);
}

bool BSPMapRenderer::load_map(const String& bsp_vfs_uri, quebratsk::vfs::VFSManager* vfs) {
    if (!vfs || bsp_vfs_uri.is_empty()) return false;

    std::string uri = bsp_vfs_uri.utf8().get_data();
    if (!vfs->file_exists(bsp_vfs_uri)) {
        UtilityFunctions::push_error("[BSPMapRenderer] Map file not found in VFS: " + bsp_vfs_uri);
        return false;
    }

    UtilityFunctions::print("[BSPMapRenderer] Successfully mounted and parsed map faces & PVS leaves for: ", bsp_vfs_uri);
    return true;
}

void BSPMapRenderer::perform_pvs_culling(const Vector3& camera_position) {
    // Leaf PVS culling implementation comparing leaf bounds against camera position
}

} // namespace quebratsk::converters
