#include "vram_garbage_collector.h"

namespace quebratsk::vfs {

using namespace godot;

VRAMGarbageCollector::VRAMGarbageCollector() : _max_idle_time_msec(60000) {}

VRAMGarbageCollector::~VRAMGarbageCollector() {
    stop();
}

void VRAMGarbageCollector::register_resource(const std::string& vfs_path, Ref<Resource> resource) {
    if (resource.is_null()) return;

    std::lock_guard<std::mutex> lock(_mutex);
    _tracked_resources[vfs_path] = { resource, std::chrono::steady_clock::now() };
}

void VRAMGarbageCollector::ping_resource(const std::string& vfs_path) {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _tracked_resources.find(vfs_path);
    if (it != _tracked_resources.end()) {
        it->second.last_accessed = std::chrono::steady_clock::now();
    }
}

void VRAMGarbageCollector::start(uint64_t max_idle_time_msec) {
    _max_idle_time_msec = max_idle_time_msec;

    // jthread: automatically joins on destruction, supports cooperative stop via stop_token
    _worker_thread = std::jthread([this](std::stop_token stoken) {
        while (!stoken.stop_requested()) {
            // Sleep in 1-second increments for responsive shutdown
            for (int i = 0; i < 5 && !stoken.stop_requested(); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            if (stoken.stop_requested()) break;

            _gc_loop();
        }
    });
}

void VRAMGarbageCollector::stop() {
    _worker_thread.request_stop();
    if (_worker_thread.joinable()) {
        _worker_thread.join();
    }
}

void VRAMGarbageCollector::_gc_loop() {
    auto now = std::chrono::steady_clock::now();
    auto max_idle = std::chrono::milliseconds(_max_idle_time_msec);

    // Phase 1: Collect eviction candidates under lock (background thread)
    std::vector<std::string> candidates;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& [path, record] : _tracked_resources) {
            if ((now - record.last_accessed) > max_idle) {
                candidates.push_back(path);
            }
        }
    }

    // Phase 2: Stage candidates for main thread to consume
    if (!candidates.empty()) {
        std::lock_guard<std::mutex> lock(_eviction_mutex);
        _pending_evictions.insert(_pending_evictions.end(),
            std::make_move_iterator(candidates.begin()),
            std::make_move_iterator(candidates.end()));
    }
}

void VRAMGarbageCollector::evict_expired_resources() {
    // MUST be called from main thread (e.g. via _process or call_deferred)
    // This is where Godot Resource destruction actually happens safely.
    std::vector<std::string> to_evict;
    {
        std::lock_guard<std::mutex> lock(_eviction_mutex);
        std::swap(to_evict, _pending_evictions);
    }

    if (to_evict.empty()) return;

    std::lock_guard<std::mutex> lock(_mutex);
    for (const auto& path : to_evict) {
        auto it = _tracked_resources.find(path);
        if (it != _tracked_resources.end()) {
            // Safe to unref on main thread — Resource destructor can talk to RenderingServer
            it->second.resource.unref();
            _tracked_resources.erase(it);
        }
    }
}

} // namespace quebratsk::vfs
