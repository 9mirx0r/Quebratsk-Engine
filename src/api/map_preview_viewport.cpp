#include "map_preview_viewport.h"

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace quebratsk::api {

using namespace godot;

MapPreviewViewport::MapPreviewViewport() {
    // Only local property setup here. Creating child nodes and calling make_current()
    // from a GDExtension constructor is unsafe: the object is not fully constructed,
    // it is not in the tree yet, and when the class is instantiated from a .tscn the
    // scene's own children are added afterwards — producing a duplicate camera and
    // hijacking the active camera before the viewport exists.
    set_update_mode(UPDATE_ALWAYS);
}

void MapPreviewViewport::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_camera_pose", "position", "rotation_deg"), &MapPreviewViewport::set_camera_pose);
}

void MapPreviewViewport::_ready() {
    // Respect a camera authored in the scene rather than adding a second one.
    if (Camera3D* existing = Object::cast_to<Camera3D>(find_child("PreviewCamera", false, false))) {
        _camera_id = existing->get_instance_id();
        return;
    }

    Camera3D* cam = memnew(Camera3D);
    cam->set_name("PreviewCamera");
    add_child(cam);
    _camera_id = cam->get_instance_id();
    cam->make_current();
}

void MapPreviewViewport::set_camera_pose(const Vector3& position, const Vector3& rotation_deg) {
    if (_camera_id == 0) return;

    Camera3D* cam = Object::cast_to<Camera3D>(ObjectDB::get_instance(_camera_id));
    if (!cam) {
        UtilityFunctions::printerr("[MapPreviewViewport] Preview camera no longer exists.");
        return;
    }

    cam->set_position(position);
    cam->set_rotation_degrees(rotation_deg);
}

} // namespace quebratsk::api
