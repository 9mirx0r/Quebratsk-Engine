#include "vfs_telemetry.h"

namespace quebratsk::vfs {

VFSTelemetry& VFSTelemetry::instance() {
    static VFSTelemetry s_instance;
    return s_instance;
}

VFSTelemetryMetrics VFSTelemetry::get_metrics() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_metrics;
}

void VFSTelemetry::record_parse_time(float duration_ms) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_metrics.last_parse_duration_ms = duration_ms;
}

void VFSTelemetry::add_mapped_bytes(uint64_t bytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_metrics.total_memory_mapped_bytes += bytes;
}

} // namespace quebratsk::vfs
