#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <mutex>
#include <thread>
#include <atomic>

namespace quebratsk::vfs {

class VRAMGarbageCollector {
public:
    VRAMGarbageCollector();
    ~VRAMGarbageCollector();

    /// Register a texture or mesh to be tracked by the GC
    void register_resource(const std::string& vfs_path, godot::Ref<godot::Resource> resource);

    /// Update the "last accessed" timestamp for a resource
    void ping_resource(const std::string& vfs_path);
    
    /// Starts the background eviction thread
    void start(uint64_t max_idle_time_msec = 60000); // 60 seconds default
    
    /// Stops the background thread
    void stop();

private:
    void _gc_loop();

    struct GCRecord {
        godot::Ref<godot::Resource> resource;
        uint64_t last_accessed_msec;
    };

    std::unordered_map<std::string, GCRecord> _tracked_resources;
    std::mutex _mutex;
    
    std::atomic<bool> _running;
    uint64_t _max_idle_time_msec;
    std::thread _worker_thread;
};

} // namespace quebratsk::vfs
