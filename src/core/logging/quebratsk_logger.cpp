#include "quebratsk_logger.h"
#include <godot_cpp/variant/utility_functions.hpp>

namespace quebratsk::logging {

using namespace godot;

void QuebratskLogger::_bind_methods() {
    ClassDB::bind_static_method("QuebratskLogger", D_METHOD("log_error", "message", "vfs_path"), &QuebratskLogger::log_error);
    ClassDB::bind_static_method("QuebratskLogger", D_METHOD("log_warning", "message", "vfs_path"), &QuebratskLogger::log_warning);
}

void QuebratskLogger::log_error(const String& message, const String& vfs_path) {
    String formatted = "[Quebratsk Error] " + message;
    if (!vfs_path.is_empty()) {
        formatted += " -> Asset: [ " + vfs_path + " ]";
    }
    UtilityFunctions::push_error(formatted);
}

void QuebratskLogger::log_warning(const String& message, const String& vfs_path) {
    String formatted = "[Quebratsk Warning] " + message;
    if (!vfs_path.is_empty()) {
        formatted += " -> Asset: [ " + vfs_path + " ]";
    }
    UtilityFunctions::push_warning(formatted);
}

} // namespace quebratsk::logging
