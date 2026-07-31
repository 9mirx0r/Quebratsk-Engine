#include "quebratsk_settings.h"
#include <godot_cpp/classes/project_settings.hpp>

#include <algorithm>
#include <thread>

namespace quebratsk::config {

using namespace godot;

void QuebratskSettings::_bind_methods() {
    // Static helper; nothing to expose to GDScript.
}

void QuebratskSettings::register_settings() {
    ProjectSettings* ps = ProjectSettings::get_singleton();
    if (!ps) return;

    if (!ps->has_setting(kMaxBackgroundThreads)) {
        ps->set_setting(kMaxBackgroundThreads, default_background_threads());
    }
    ps->set_initial_value(kMaxBackgroundThreads, default_background_threads());
}

int QuebratskSettings::default_background_threads() {
    const unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) return 4; // unknowable on this platform
    return std::clamp(static_cast<int>(hw) - 1, 1, 8);
}

int QuebratskSettings::max_background_threads() {
    ProjectSettings* ps = ProjectSettings::get_singleton();
    if (!ps || !ps->has_setting(kMaxBackgroundThreads)) {
        return default_background_threads();
    }
    // User-editable, so it arrives unvalidated: 0 would stall every async import forever,
    // and a large value would spawn a thread per queued asset.
    return std::clamp(int(ps->get_setting(kMaxBackgroundThreads)), 1, 32);
}

} // namespace quebratsk::config
