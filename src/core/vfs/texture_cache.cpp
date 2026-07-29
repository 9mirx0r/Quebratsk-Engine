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

void TextureCache::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_material_cache.clear();
}

} // namespace quebratsk::vfs
