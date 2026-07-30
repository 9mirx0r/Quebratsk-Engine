#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>
#include "../core/vfs/vfs_manager.h"
#include <string>
#include <vector>

namespace quebratsk::converters {

class BSPMapRenderer : public godot::Node3D {
    GDCLASS(BSPMapRenderer, godot::Node3D)

protected:
    static void _bind_methods();

public:
    BSPMapRenderer() = default;
    ~BSPMapRenderer() override = default;

    /// Loads and builds a full 3D map from a BSP v30 (GoldSrc) or VBSP (Source) URI
    /// Returns true if map rendered successfully into scene tree.
    bool load_map(const godot::String& bsp_vfs_uri, quebratsk::vfs::VFSManager* vfs);

    /// Perform leaf PVS (Potentially Visible Set) visibility culling against camera position
    void perform_pvs_culling(const godot::Vector3& camera_position);
};

} // namespace quebratsk::converters
