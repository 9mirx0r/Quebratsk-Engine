#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace quebratsk::vfs {

struct VFSTelemetryMetrics {
    uint64_t total_memory_mapped_bytes = 0;
    uint32_t active_mounted_containers = 0;
    uint32_t cached_textures_count = 0;
    uint64_t total_decompressed_bytes = 0;
    float last_parse_duration_ms = 0.0f;
};

class VFSTelemetry {
public:
    static VFSTelemetry& instance();

    /// Get current telemetry metrics
    [[nodiscard]] VFSTelemetryMetrics get_metrics();

    /// Record parse timing measurement
    void record_parse_time(float duration_ms);

    /// Update memory-mapped byte totals
    void add_mapped_bytes(uint64_t bytes);

private:
    VFSTelemetry() = default;
    std::mutex m_mutex;
    VFSTelemetryMetrics m_metrics;
};

} // namespace quebratsk::vfs
