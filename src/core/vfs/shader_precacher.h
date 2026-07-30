#pragma once

#include <string>
#include <vector>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/ref.hpp>

namespace quebratsk::vfs {

class ShaderPrecacher {
public:
    /// Traverse the mounted VFS to find all unique material definitions
    /// and force the Godot RenderingServer to compile them.
    static void precache_all_materials(const std::vector<std::string>& vfs_material_paths);

private:
    static godot::Ref<godot::ShaderMaterial> _create_dummy_material(const std::string& path);
};

} // namespace quebratsk::vfs
