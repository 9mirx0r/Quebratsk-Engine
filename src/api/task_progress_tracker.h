#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/string.hpp>
#include <atomic>
#include <mutex>
#include <string>

namespace quebratsk::api {

class TaskProgressTracker : public godot::Object {
    GDCLASS(TaskProgressTracker, godot::Object)

protected:
    static void _bind_methods();

public:
    TaskProgressTracker();
    ~TaskProgressTracker() = default;

    /// Sets the current task label and progress (0.0 to 1.0)
    void set_progress(const godot::String& task_name, float progress);

    /// Get current progress percentage (0.0 to 1.0)
    float get_progress() const;

    /// Get current active task status text
    godot::String get_status_text() const;

    /// Reset tracker when tasks finish
    void reset();

private:
    std::atomic<float> _current_progress{0.0f};
    std::string _current_task_name;
    mutable std::mutex _mutex;
};

} // namespace quebratsk::api
