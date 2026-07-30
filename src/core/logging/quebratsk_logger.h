#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/string.hpp>

namespace quebratsk::logging {

class QuebratskLogger : public godot::Object {
    GDCLASS(QuebratskLogger, godot::Object)

protected:
    static void _bind_methods();

public:
    QuebratskLogger() = default;
    ~QuebratskLogger() = default;

    /// Print formatted human-readable error with clickable VFS path
    static void log_error(const godot::String& message, const godot::String& vfs_path);
    
    /// Print formatted human-readable warning with clickable VFS path
    static void log_warning(const godot::String& message, const godot::String& vfs_path);
};

} // namespace quebratsk::logging
