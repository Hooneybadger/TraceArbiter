#include "tracearbiter/calibration.hpp"

namespace tracearbiter {

std::vector<TraceSource> apply_calibration(std::vector<TraceSource> sources,
                                           const std::vector<SourceMeasurement>& measured) {
    for (auto& source : sources) {
        for (const auto& row : measured) {
            if (row.id != source.id()) {
                continue;
            }
            source.measured_events_per_sec = row.events_per_sec;
            source.measured_bytes_per_sec = row.bytes_per_sec;
            source.measured_overhead_pct = row.overhead_pct;
        }
    }
    return sources;
}

} // namespace tracearbiter
