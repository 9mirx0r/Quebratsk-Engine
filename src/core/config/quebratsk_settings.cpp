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
    
    // Set property hints so they show up as sliders/toggles in Godot UI
    Dictionary timeout_prop;
    timeout_prop["name"] = "quebratsk/performance/vram_eviction_timeout_msec";
    timeout_prop["type"] = Variant::INT;
    timeout_prop["hint"] = PROPERTY_HINT_RANGE;
    timeout_prop["hint_string"] = "10000,300000,1000";
    ps->add_custom_property_info(timeout_prop);
    
    Dictionary threads_prop;
    threads_prop["name"] = "quebratsk/performance/max_background_threads";
    threads_prop["type"] = Variant::INT;
    threads_prop["hint"] = PROPERTY_HINT_RANGE;
    threads_prop["hint_string"] = "1,16,1";
    ps->add_custom_property_info(threads_prop);
}

} // namespace quebratsk::config
