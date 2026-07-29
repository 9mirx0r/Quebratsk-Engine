#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>

#include <mutex>
#include <string>
#include <unordered_map>

namespace quebratsk::vfs {

class TextureCache {
public:
    static TextureCache& instance();

    /// Get cached StandardMaterial3D or return empty Ref if not cached
    [[nodiscard]] godot::Ref<godot::StandardMaterial3D> get_material(const std::string& key);

    /// Store StandardMaterial3D in cache
    void set_material(const std::string& key, godot::Ref<godot::StandardMaterial3D> material);

    /// Clear cached resources
    void clear();

private:
    TextureCache() = default;
    std::mutex m_mutex;
    std::unordered_map<std::string, godot::Ref<godot::StandardMaterial3D>> m_material_cache;
};

} // namespace quebratsk::vfs
