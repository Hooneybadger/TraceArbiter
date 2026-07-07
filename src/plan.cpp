#include "tracearbiter/plan.hpp"
#include "tracearbiter/yaml.hpp"

namespace tracearbiter {
namespace {

std::optional<std::uint64_t> to_u64(const std::optional<std::string>& text) {
    if (!text) {
        return std::nullopt;
    }
    try {
        std::size_t idx = 0;
        auto value = std::stoull(*text, &idx);
        if (idx != text->size()) {
            return std::nullopt;
        }
        return value;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void append_source(TracePlan& plan, const TraceSource& source, std::string instance,
                   std::uint64_t buffer_kb) {
    PlannedSource row;
    row.source = source.id();
    row.instance = std::move(instance);
    row.enabled = true;
    row.buffer_kb = buffer_kb;
    plan.sources.push_back(std::move(row));
}

} // namespace

Json plan_to_json(const TracePlan& plan) {
    Json json = Json::object();
    json["feasible"] = Json::boolean(plan.feasible);
    json["estimated_overhead_pct"] = Json::number(plan.estimated_overhead_pct);
    json["estimated_trace_mb_per_sec"] = Json::number(plan.estimated_trace_mb_per_sec);
    json["total_buffer_kb"] = Json::integer(static_cast<std::int64_t>(plan.total_buffer_kb));
    json["reasons"] = Json::array();
    for (const auto& reason : plan.reasons) {
        json["reasons"].push(Json::string(reason));
    }
    json["sources"] = Json::array();
    for (const auto& source : plan.sources) {
        Json row = Json::object();
        row["source"] = Json::string(source.source);
        row["instance"] = Json::string(source.instance);
        row["enabled"] = Json::boolean(source.enabled);
        row["buffer_kb"] = Json::integer(static_cast<std::int64_t>(source.buffer_kb));
        json["sources"].push(std::move(row));
    }
    return json;
}

Result<TracePlan> load_plan(const std::string& path) {
    auto yaml = load_yaml_file(path);
    if (!yaml) {
        return Result<TracePlan>::err(yaml.error());
    }
    const YamlValue& root = yaml.value();
    TracePlan plan;
    if (auto total = to_u64(root.scalar("total_buffer_kb"))) {
        plan.total_buffer_kb = *total;
    }
    if (auto feasible = root.scalar("feasible")) {
        plan.feasible = *feasible == "true" || *feasible == "1";
    } else {
        plan.feasible = true;
    }
    const YamlValue* sources = root.child("sources");
    if (!sources || !sources->as_map()) {
        return Result<TracePlan>::err(make_error("PLAN_SCHEMA", "missing sources map"));
    }
    for (const auto& [id, node] : *sources->as_map()) {
        PlannedSource row;
        row.source = id;
        row.instance = node.scalar("instance").value_or("full");
        if (auto enabled = node.scalar("enabled")) {
            row.enabled = *enabled == "true" || *enabled == "1";
        } else {
            row.enabled = true;
        }
        row.buffer_kb = to_u64(node.scalar("buffer_kb")).value_or(0);
        plan.sources.push_back(std::move(row));
    }
    return Result<TracePlan>::ok(std::move(plan));
}

TracePlan full_single_plan(const std::vector<TraceSource>& sources, const Budget& budget) {
    TracePlan plan;
    plan.feasible = true;
    plan.total_buffer_kb = budget.total_buffer_kb;
    plan.reasons.push_back("BASELINE_B1_FULL_SINGLE_BUFFER");
    for (const auto& source : sources) {
        if (!source.available) {
            continue;
        }
        append_source(plan, source, "full", budget.total_buffer_kb);
        if (source.measured_overhead_pct) {
            plan.estimated_overhead_pct += *source.measured_overhead_pct;
        }
        if (source.measured_bytes_per_sec) {
            plan.estimated_trace_mb_per_sec += *source.measured_bytes_per_sec / (1024.0 * 1024.0);
        }
    }
    return plan;
}

TracePlan static_split_plan(const std::vector<TraceSource>& sources, const Budget& budget) {
    TracePlan plan;
    plan.feasible = true;
    plan.total_buffer_kb = budget.total_buffer_kb;
    plan.reasons.push_back("BASELINE_B2_STATIC_SPLIT");
    auto critical_kb = static_cast<std::uint64_t>(budget.total_buffer_kb * 0.7);
    if (critical_kb >= budget.total_buffer_kb) {
        critical_kb = budget.total_buffer_kb - 1;
    }
    if (critical_kb == 0) {
        critical_kb = budget.total_buffer_kb;
    }
    const auto bulk_kb = budget.total_buffer_kb - critical_kb;
    bool has_bulk = false;
    for (const auto& source : sources) {
        if (source.available && source.priority == Priority::Bulk) {
            has_bulk = true;
            break;
        }
    }
    for (const auto& source : sources) {
        if (!source.available) {
            continue;
        }
        if (source.priority == Priority::Bulk && has_bulk && bulk_kb > 0) {
            append_source(plan, source, "bulk", bulk_kb);
        } else {
            append_source(plan, source, "critical", has_bulk ? critical_kb : budget.total_buffer_kb);
        }
        if (source.measured_overhead_pct) {
            plan.estimated_overhead_pct += *source.measured_overhead_pct;
        }
        if (source.measured_bytes_per_sec) {
            plan.estimated_trace_mb_per_sec += *source.measured_bytes_per_sec / (1024.0 * 1024.0);
        }
    }
    return plan;
}

TracePlan critical_only_plan(const std::vector<TraceSource>& sources, std::uint64_t buffer_kb) {
    TracePlan plan;
    plan.feasible = true;
    plan.total_buffer_kb = buffer_kb;
    plan.reasons.push_back("REFERENCE_CRITICAL_ONLY");
    for (const auto& source : sources) {
        if (!source.available || source.priority != Priority::Critical) {
            continue;
        }
        append_source(plan, source, "critical", buffer_kb);
    }
    return plan;
}

} // namespace tracearbiter
