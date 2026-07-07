#pragma once

#include "tracearbiter/source.hpp"

#include <optional>
#include <string>
#include <vector>

namespace tracearbiter {

struct SourceMeasurement {
    SourceId id;
    std::optional<double> events_per_sec;
    std::optional<double> bytes_per_sec;
    std::optional<double> overhead_pct;
};

std::vector<TraceSource> apply_calibration(std::vector<TraceSource> sources,
                                           const std::vector<SourceMeasurement>& measured);

} // namespace tracearbiter
