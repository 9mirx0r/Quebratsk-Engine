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

    // NOT IMPLEMENTED. This used to print "Successfully mounted and parsed map faces
    // & PVS leaves" and return true without parsing anything, so callers had no way to
    // tell that nothing had happened. Report the truth instead.
    UtilityFunctions::push_error(
        "[BSPMapRenderer] load_map() is not implemented (no face extraction, no PVS). "
        "Use UnifiedAssetImporter.load_mesh() for BSP geometry.");
    return false;
}

void BSPMapRenderer::perform_pvs_culling(const Vector3& camera_position) {
    // NOT IMPLEMENTED: leaf PVS decompression and per-leaf visibility are missing.
    // Kept as a no-op so existing scripts do not break, but it culls nothing.
    UtilityFunctions::push_warning("[BSPMapRenderer] perform_pvs_culling() is a no-op.");
}

} // namespace quebratsk::converters
