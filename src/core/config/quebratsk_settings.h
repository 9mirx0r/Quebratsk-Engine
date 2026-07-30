#pragma once

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/object.hpp>

namespace quebratsk::config {

class QuebratskSettings : public godot::Object {
    GDCLASS(QuebratskSettings, godot::Object)

protected:
    static void _bind_methods();

public:
    static void register_settings();
};

} // namespace quebratsk::config
