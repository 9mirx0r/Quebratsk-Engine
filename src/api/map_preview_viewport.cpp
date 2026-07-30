#include "map_preview_viewport.h"
#include <godot_cpp/classes/node3d.hpp>

namespace quebratsk::api {

using namespace godot;

MapPreviewViewport::MapPreviewViewport() {
    set_update_mode(UPDATE_ALWAYS);
    
    _camera = memnew(Camera3D);
    add_child(_camera);
    _camera->make_current();
}

void MapPreviewViewport::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_camera_pose", "position", "rotation_deg"), &MapPreviewViewport::set_camera_pose);
}

void MapPreviewViewport::set_camera_pose(const Vector3& position, const Vector3& rotation_deg) {
    if (_camera) {
        _camera->set_position(position);
        _camera->set_rotation_degrees(rotation_deg);
    }
}

} // namespace quebratsk::api
