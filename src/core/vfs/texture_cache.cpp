#include "texture_cache.h"

namespace quebratsk::vfs {

using namespace godot;

TextureCache& TextureCache::instance() {
    static TextureCache s_instance;
    return s_instance;
}

Ref<StandardMaterial3D> TextureCache::get_material(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_material_cache.find(key);
    if (it != m_material_cache.end()) {
        return it->second;
    }
    return {};
}

void TextureCache::set_material(const std::string& key, Ref<StandardMaterial3D> material) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_material_cache[key] = material;
}

Ref<Texture2D> TextureCache::get_texture(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_texture_cache.find(key);
    if (it != m_texture_cache.end()) {
        return it->second;
    }
    return {};
}

void TextureCache::set_texture(const std::string& key, Ref<Texture2D> texture) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_texture_cache[key] = texture;
}

bool TextureCache::has_texture_entry(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_texture_cache.contains(key);
}

void TextureCache::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_material_cache.clear();
    m_texture_cache.clear();
}

} // namespace quebratsk::vfs
