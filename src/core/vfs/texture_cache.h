#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>

#include <mutex>
#include <string>
#include <unordered_map>

// NOTE: this singleton holds Godot Refs in a function-local static, which is destroyed
// at library unload — after the engine is gone. register_types.cpp calls clear() from
// uninitialize_quebratsk_module() so nothing survives that long. Any new Ref member
// added here must be released by clear().

namespace quebratsk::vfs {

class TextureCache {
public:
    static TextureCache& instance();

    /// Get cached StandardMaterial3D or return empty Ref if not cached
    [[nodiscard]] godot::Ref<godot::StandardMaterial3D> get_material(const std::string& key);

    /// Store StandardMaterial3D in cache
    void set_material(const std::string& key, godot::Ref<godot::StandardMaterial3D> material);

    /// Get cached texture, or an empty Ref. Decoding a VTF is expensive and the same
    /// texture is referenced by many materials, so this avoids repeated work.
    [[nodiscard]] godot::Ref<godot::Texture2D> get_texture(const std::string& key);

    /// Store a decoded texture. A null Ref is stored deliberately to remember a failed
    /// lookup and avoid re-scanning the whole VFS index for a texture that is missing.
    void set_texture(const std::string& key, godot::Ref<godot::Texture2D> texture);

    /// True when `key` has been looked up before, successfully or not.
    [[nodiscard]] bool has_texture_entry(const std::string& key);

    /// Clear cached resources
    void clear();

private:
    TextureCache() = default;
    std::mutex m_mutex;
    std::unordered_map<std::string, godot::Ref<godot::StandardMaterial3D>> m_material_cache;
    std::unordered_map<std::string, godot::Ref<godot::Texture2D>> m_texture_cache;
};

} // namespace quebratsk::vfs
