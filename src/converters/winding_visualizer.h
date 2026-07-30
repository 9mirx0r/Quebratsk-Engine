#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace quebratsk::converters {

class WindingVisualizer : public godot::Object {
    GDCLASS(WindingVisualizer, godot::Object)

protected:
    static void _bind_methods();

public:
    WindingVisualizer() = default;
    ~WindingVisualizer() = default;

    /// Attaches a debug shader highlighting inverted back-faces in red
    static void apply_debug_winding_material(godot::MeshInstance3D* mesh_instance);
    
    /// Flips winding order of the mesh instance surfaces
    static void flip_normals_and_winding(godot::MeshInstance3D* mesh_instance);
};

} // namespace quebratsk::converters
