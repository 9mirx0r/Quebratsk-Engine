#include "import_presets.h"
#include <godot_cpp/classes/project_settings.hpp>

namespace quebratsk::config {

using namespace godot;

void ImportPresets::_bind_methods() {
    ClassDB::bind_static_method("ImportPresets", D_METHOD("apply_preset", "preset_index"), &ImportPresets::apply_preset);
    
    BIND_ENUM_CONSTANT(PresetType::MAX_PERFORMANCE);
    BIND_ENUM_CONSTANT(PresetType::RETRO_FIDELITY);
    BIND_ENUM_CONSTANT(PresetType::MAX_QUALITY);
}

void ImportPresets::apply_preset(int preset_index) {
    ProjectSettings* ps = ProjectSettings::get_singleton();
    if (!ps) return;

    PresetType type = static_cast<PresetType>(preset_index);

    switch (type) {
        case PresetType::MAX_PERFORMANCE:
            ps->set_setting("quebratsk/performance/vram_eviction_timeout_msec", 30000);
            ps->set_setting("quebratsk/performance/max_background_threads", 8);
            ps->set_setting("quebratsk/performance/enable_shader_precaching", true);
            break;

        case PresetType::RETRO_FIDELITY:
            ps->set_setting("quebratsk/performance/vram_eviction_timeout_msec", 120000);
            ps->set_setting("quebratsk/performance/max_background_threads", 2);
            ps->set_setting("quebratsk/performance/enable_shader_precaching", false);
            break;

        case PresetType::MAX_QUALITY:
            ps->set_setting("quebratsk/performance/vram_eviction_timeout_msec", 300000);
            ps->set_setting("quebratsk/performance/max_background_threads", 4);
            ps->set_setting("quebratsk/performance/enable_shader_precaching", true);
            break;
    }
}

} // namespace quebratsk::config
