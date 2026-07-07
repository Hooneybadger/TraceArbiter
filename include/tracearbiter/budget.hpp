#pragma once

#include "tracearbiter/error.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace tracearbiter {

struct Budget {
    std::uint64_t total_buffer_kb{};
    double target_overhead_pct{};
    std::optional<double> max_trace_mb_per_sec;
    double weight_trace_rate{1.0};
    double weight_overhead{1.0};
    double critical_buffer_floor{0.5};
};

Result<Budget> load_budget(const std::string& path);

} // namespace tracearbiter
