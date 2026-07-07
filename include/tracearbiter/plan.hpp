#pragma once

#include "tracearbiter/budget.hpp"
#include "tracearbiter/json.hpp"
#include "tracearbiter/source.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace tracearbiter {

struct PlannedSource {
    SourceId source;
    std::string instance;
    bool enabled{false};
    std::uint64_t buffer_kb{0};
};

struct TracePlan {
    std::vector<PlannedSource> sources;
    double estimated_overhead_pct{0};
    double estimated_trace_mb_per_sec{0};
    std::uint64_t total_buffer_kb{0};
    bool feasible{false};
    std::vector<std::string> reasons;
};

Json plan_to_json(const TracePlan& plan);
Result<TracePlan> load_plan(const std::string& path);

TracePlan static_split_plan(const std::vector<TraceSource>& sources, const Budget& budget);
TracePlan full_single_plan(const std::vector<TraceSource>& sources, const Budget& budget);
TracePlan critical_only_plan(const std::vector<TraceSource>& sources, std::uint64_t buffer_kb);

} // namespace tracearbiter
