#include "vram_garbage_collector.h"
#include <godot_cpp/classes/os.hpp>

namespace quebratsk::vfs {

using namespace godot;

VRAMGarbageCollector::VRAMGarbageCollector() : _running(false), _max_idle_time_msec(60000) {}

VRAMGarbageCollector::~VRAMGarbageCollector() {
    stop();
}

void VRAMGarbageCollector::register_resource(const std::string& vfs_path, Ref<Resource> resource) {
    if (resource.is_null()) return;
    
    std::lock_guard<std::mutex> lock(_mutex);
    _tracked_resources[vfs_path] = { resource, OS::get_singleton()->get_ticks_msec() };
}

void VRAMGarbageCollector::ping_resource(const std::string& vfs_path) {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _tracked_resources.find(vfs_path);
    if (it != _tracked_resources.end()) {
        it->second.last_accessed_msec = OS::get_singleton()->get_ticks_msec();
    }
}

void VRAMGarbageCollector::start(uint64_t max_idle_time_msec) {
    if (_running.exchange(true)) return; // Already running
    
    _max_idle_time_msec = max_idle_time_msec;
    _worker_thread = std::thread(&VRAMGarbageCollector::_gc_loop, this);
}

void VRAMGarbageCollector::stop() {
    if (!_running.exchange(false)) return;
    
    if (_worker_thread.joinable()) {
        _worker_thread.join();
    }
}

void VRAMGarbageCollector::_gc_loop() {
    while (_running) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        uint64_t current_time = OS::get_singleton()->get_ticks_msec();
        
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto it = _tracked_resources.begin(); it != _tracked_resources.end(); ) {
            // Check if resource is only referenced by our GC (reference count == 1)
            // Note: In Godot 4 GDExtension, get_reference_count() is used.
            // For safety, we also check idle time.
            if (it->second.resource->get_reference_count() <= 1 && 
                (current_time - it->second.last_accessed_msec) > _max_idle_time_msec) {
                
                // Evict from memory
                it->second.resource.unref(); 
                it = _tracked_resources.erase(it);
            } else {
                ++it;
            }
        }
    }
}

} // namespace quebratsk::vfs
