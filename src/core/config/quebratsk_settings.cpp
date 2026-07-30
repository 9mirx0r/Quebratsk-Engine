#include "quebratsk_settings.h"
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace quebratsk::config {

using namespace godot;

void QuebratskSettings::_bind_methods() {
    // No instance methods to bind for this static helper
}

void QuebratskSettings::register_settings() {
    ProjectSettings* ps = ProjectSettings::get_singleton();
    if (!ps) return;

    // Default VRAM Eviction Timeout
    if (!ps->has_setting("quebratsk/performance/vram_eviction_timeout_msec")) {
        ps->set_setting("quebratsk/performance/vram_eviction_timeout_msec", 60000);
    }
    ps->set_initial_value("quebratsk/performance/vram_eviction_timeout_msec", 60000);

    // Max Background Threads
    if (!ps->has_setting("quebratsk/performance/max_background_threads")) {
        ps->set_setting("quebratsk/performance/max_background_threads", 4);
    }
    ps->set_initial_value("quebratsk/performance/max_background_threads", 4);

    // Enable Shader Pre-caching
    if (!ps->has_setting("quebratsk/performance/enable_shader_precaching")) {
        ps->set_setting("quebratsk/performance/enable_shader_precaching", true);
    }
    ps->set_initial_value("quebratsk/performance/enable_shader_precaching", true);
    
}

} // namespace quebratsk::config
