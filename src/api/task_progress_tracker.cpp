#include "task_progress_tracker.h"
#include <algorithm>

namespace quebratsk::api {

using namespace godot;

TaskProgressTracker::TaskProgressTracker() {}

void TaskProgressTracker::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_progress", "task_name", "progress"), &TaskProgressTracker::set_progress);
    ClassDB::bind_method(D_METHOD("get_progress"), &TaskProgressTracker::get_progress);
    ClassDB::bind_method(D_METHOD("get_status_text"), &TaskProgressTracker::get_status_text);
    ClassDB::bind_method(D_METHOD("reset"), &TaskProgressTracker::reset);
}

void TaskProgressTracker::set_progress(const String& task_name, float progress) {
    // Publish label and value under the same lock. Storing the atomic outside it let a
    // reader observe the new percentage paired with the previous task's name.
    std::lock_guard<std::mutex> lock(_mutex);
    _current_task_name = task_name.utf8().get_data();
    _current_progress.store(std::clamp(progress, 0.0f, 1.0f), std::memory_order_relaxed);
}

float TaskProgressTracker::get_progress() const {
    return _current_progress.load(std::memory_order_relaxed);
}

String TaskProgressTracker::get_status_text() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return String(_current_task_name.c_str());
}

void TaskProgressTracker::reset() {
    std::lock_guard<std::mutex> lock(_mutex);
    _current_task_name.clear();
    _current_progress.store(0.0f, std::memory_order_relaxed);
}

} // namespace quebratsk::api
