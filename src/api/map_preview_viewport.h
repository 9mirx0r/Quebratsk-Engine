#pragma once

#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace quebratsk::api {

class MapPreviewViewport : public godot::SubViewport {
    GDCLASS(MapPreviewViewport, godot::SubViewport)

protected:
    static void _bind_methods();

public:
    MapPreviewViewport();
    ~MapPreviewViewport() = default;

    /// Configures the viewport camera position and rotation for live editor fly-through preview
    void set_camera_pose(const godot::Vector3& position, const godot::Vector3& rotation_deg);

private:
    godot::Camera3D* _camera = nullptr;
};

} // namespace quebratsk::api
