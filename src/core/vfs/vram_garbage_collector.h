#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <unordered_map>
#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <thread>
#include <chrono>

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
    void start(uint64_t max_idle_time_msec = 60000);

    /// Stops the background thread
    void stop();

    /// Called from main thread to actually free expired resources (via call_deferred)
    void evict_expired_resources();

private:
    void _gc_loop();

    struct GCRecord {
        godot::Ref<godot::Resource> resource;
        std::chrono::steady_clock::time_point last_accessed;
    };

    std::unordered_map<std::string, GCRecord> _tracked_resources;
    std::mutex _mutex;

    // Eviction candidates collected by background thread, consumed by main thread
    std::vector<std::string> _pending_evictions;
    std::mutex _eviction_mutex;

    std::jthread _worker_thread;
    uint64_t _max_idle_time_msec;
};

} // namespace quebratsk::vfs
