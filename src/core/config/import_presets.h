#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace quebratsk::config {

enum class PresetType {
    MAX_PERFORMANCE,
    RETRO_FIDELITY,
    MAX_QUALITY
};

class ImportPresets : public godot::Object {
    GDCLASS(ImportPresets, godot::Object)

protected:
    static void _bind_methods();

public:
    ImportPresets() = default;
    ~ImportPresets() = default;

    /// Apply one-click preset settings to ProjectSettings
    static void apply_preset(int preset_index);
};

} // namespace quebratsk::config
