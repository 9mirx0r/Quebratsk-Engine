#pragma once

#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstdint>

namespace quebratsk::api {

class MapPreviewViewport : public godot::SubViewport {
    GDCLASS(MapPreviewViewport, godot::SubViewport)

protected:
    static void _bind_methods();

public:
    MapPreviewViewport();
    ~MapPreviewViewport() = default;

    void _ready() override;

    /// Configures the viewport camera position and rotation for live editor fly-through preview
    void set_camera_pose(const godot::Vector3& position, const godot::Vector3& rotation_deg);

private:
    /// Stored as an ObjectID rather than a raw pointer so deleting the camera in the
    /// editor cannot leave a dangling reference behind.
    uint64_t _camera_id = 0;
};

} // namespace quebratsk::api
