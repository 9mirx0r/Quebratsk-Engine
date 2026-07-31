#pragma once

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/object.hpp>

namespace quebratsk::config {

/// Project settings the engine actually honours.
///
/// Two settings used to be declared here — vram_eviction_timeout_msec and
/// enable_shader_precaching — along with an ImportPresets class that wrote all of them.
/// Nothing read any of them: TextureCache has no eviction and there is no shader
/// precaching. They showed up in the Project Settings UI, could be changed and saved, and
/// did nothing. A setting that has no effect is worse than a missing one, because the
/// user believes they have configured something.
class QuebratskSettings : public godot::Object {
    GDCLASS(QuebratskSettings, godot::Object)

protected:
    static void _bind_methods();

public:
    static constexpr const char* kMaxBackgroundThreads =
        "quebratsk/performance/max_background_threads";

    static void register_settings();

    /// Cores minus one, clamped to [1, 8]: importing should not make the editor
    /// unresponsive on a small machine or oversubscribe a large one.
    [[nodiscard]] static int default_background_threads();

    /// The configured cap, clamped to a sane range. Read at the point of use rather than
    /// cached, so changing it in Project Settings takes effect without a restart.
    [[nodiscard]] static int max_background_threads();
};

} // namespace quebratsk::config
