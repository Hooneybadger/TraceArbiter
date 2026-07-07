#include "tracearbiter/planner.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <sstream>

namespace tracearbiter {
namespace {

constexpr const char* kCriticalInstance = "critical";
constexpr const char* kBulkInstance = "bulk";

std::string instance_for(Priority priority) {
    return priority == Priority::Bulk ? kBulkInstance : kCriticalInstance;
}

double value_or_nan(const std::optional<double>& value) {
    return value.value_or(0);
}

bool has_cost(const TraceSource& source) {
    return source.measured_bytes_per_sec.has_value() || source.measured_overhead_pct.has_value();
}

double normalized_cost(const TraceSource& source, double max_bytes, double max_overhead, const Budget& budget) {
    double trace = 0;
    if (max_bytes > 0 && source.measured_bytes_per_sec) {
        trace = *source.measured_bytes_per_sec / max_bytes;
    }
    double overhead = 0;
    if (max_overhead > 0 && source.measured_overhead_pct) {
        overhead = *source.measured_overhead_pct / max_overhead;
    }
    return budget.weight_trace_rate * trace + budget.weight_overhead * overhead;
}

struct Totals {
    double overhead{0};
    double bytes_per_sec{0};
};

void add_source(Totals& totals, const TraceSource& source) {
    totals.overhead += value_or_nan(source.measured_overhead_pct);
    totals.bytes_per_sec += value_or_nan(source.measured_bytes_per_sec);
}

bool within_budget(const Totals& totals, const Budget& budget) {
    if (totals.overhead > budget.target_overhead_pct) {
        return false;
    }
    if (budget.max_trace_mb_per_sec) {
        const double mb = totals.bytes_per_sec / (1024.0 * 1024.0);
        if (mb > *budget.max_trace_mb_per_sec) {
            return false;
        }
    }
    return true;
}

void append_enabled(TracePlan& plan, const std::vector<const TraceSource*>& selected,
                    const std::map<std::string, std::uint64_t>& buffers) {
    for (const TraceSource* source : selected) {
        PlannedSource row;
        row.source = source->id();
        row.instance = instance_for(source->priority);
        row.enabled = true;
        auto it = buffers.find(row.instance);
        row.buffer_kb = it == buffers.end() ? 0 : it->second;
        plan.sources.push_back(std::move(row));
    }
}

std::map<std::string, std::uint64_t> allocate_buffers(const std::vector<const TraceSource*>& selected,
                                                      const Budget& budget) {
    double critical_rate = 0;
    double bulk_rate = 0;
    bool has_critical = false;
    bool has_bulk = false;
    for (const TraceSource* source : selected) {
        const double rate = value_or_nan(source->measured_bytes_per_sec);
        if (source->priority == Priority::Bulk) {
            has_bulk = true;
            bulk_rate += rate;
        } else {
            has_critical = true;
            critical_rate += rate;
        }
    }
    std::map<std::string, std::uint64_t> buffers;
    if (has_critical && !has_bulk) {
        buffers[kCriticalInstance] = budget.total_buffer_kb;
        return buffers;
    }
    if (has_bulk && !has_critical) {
        buffers[kBulkInstance] = budget.total_buffer_kb;
        return buffers;
    }
    const double total_rate = critical_rate + bulk_rate;
    double critical_share = total_rate > 0 ? critical_rate / total_rate : 0.5;
    critical_share = std::max(critical_share, budget.critical_buffer_floor);
    if (critical_share > 1) {
        critical_share = 1;
    }
    auto critical_kb = static_cast<std::uint64_t>(std::llround(critical_share * static_cast<double>(budget.total_buffer_kb)));
    if (critical_kb > budget.total_buffer_kb) {
        critical_kb = budget.total_buffer_kb;
    }
    if (critical_kb == budget.total_buffer_kb && has_bulk) {
        critical_kb = budget.total_buffer_kb - 1;
    }
    buffers[kCriticalInstance] = critical_kb;
    buffers[kBulkInstance] = budget.total_buffer_kb - critical_kb;
    return buffers;
}

} // namespace

TracePlan compose_plan(const std::vector<TraceSource>& sources, const Budget& budget) {
    TracePlan plan;
    plan.total_buffer_kb = budget.total_buffer_kb;

    std::vector<const TraceSource*> critical;
    std::vector<const TraceSource*> useful;
    std::vector<const TraceSource*> bulk;
    bool missing_calibration = false;
    for (const auto& source : sources) {
        if (!source.available) {
            plan.reasons.push_back("SOURCE_UNAVAILABLE:" + source.id());
            continue;
        }
        if (!has_cost(source)) {
            missing_calibration = true;
        }
        switch (source.priority) {
        case Priority::Critical:
            critical.push_back(&source);
            break;
        case Priority::Useful:
            useful.push_back(&source);
            break;
        case Priority::Bulk:
            bulk.push_back(&source);
            break;
        }
    }

    if (missing_calibration) {
        plan.reasons.push_back("INCOMPLETE_CALIBRATION");
    }

    if (critical.empty() && useful.empty() && bulk.empty()) {
        plan.feasible = true;
        plan.reasons.push_back("NO_AVAILABLE_SOURCES");
        return plan;
    }

    double max_bytes = 0;
    double max_overhead = 0;
    for (const auto& source : sources) {
        if (source.measured_bytes_per_sec) {
            max_bytes = std::max(max_bytes, *source.measured_bytes_per_sec);
        }
        if (source.measured_overhead_pct) {
            max_overhead = std::max(max_overhead, *source.measured_overhead_pct);
        }
    }

    auto by_cost = [&](const TraceSource* left, const TraceSource* right) {
        const double left_cost = normalized_cost(*left, max_bytes, max_overhead, budget);
        const double right_cost = normalized_cost(*right, max_bytes, max_overhead, budget);
        if (left_cost == right_cost) {
            return left->id() < right->id();
        }
        return left_cost < right_cost;
    };
    std::sort(useful.begin(), useful.end(), by_cost);
    std::sort(bulk.begin(), bulk.end(), by_cost);
    std::sort(critical.begin(), critical.end(), [](const TraceSource* left, const TraceSource* right) {
        return left->id() < right->id();
    });

    std::vector<const TraceSource*> selected = critical;
    Totals totals;
    for (const TraceSource* source : selected) {
        add_source(totals, *source);
    }

    if (!within_budget(totals, budget)) {
        plan.feasible = false;
        plan.reasons.push_back("CRITICAL_SOURCES_EXCEED_BUDGET");
        plan.estimated_overhead_pct = totals.overhead;
        plan.estimated_trace_mb_per_sec = totals.bytes_per_sec / (1024.0 * 1024.0);
        auto buffers = allocate_buffers(selected, budget);
        append_enabled(plan, selected, buffers);
        return plan;
    }

    auto try_add = [&](const std::vector<const TraceSource*>& candidates) {
        for (const TraceSource* source : candidates) {
            Totals trial = totals;
            add_source(trial, *source);
            if (!within_budget(trial, budget)) {
                plan.reasons.push_back("SKIPPED_OVER_BUDGET:" + source->id());
                continue;
            }
            selected.push_back(source);
            totals = trial;
        }
    };
    try_add(useful);
    try_add(bulk);

    plan.feasible = true;
    plan.estimated_overhead_pct = totals.overhead;
    plan.estimated_trace_mb_per_sec = totals.bytes_per_sec / (1024.0 * 1024.0);
    auto buffers = allocate_buffers(selected, budget);
    append_enabled(plan, selected, buffers);
    return plan;
}

} // namespace tracearbiter
