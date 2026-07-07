#include "tracearbiter/budget.hpp"
#include "tracearbiter/yaml.hpp"

#include <charconv>
#include <optional>

namespace tracearbiter {
namespace {

std::optional<double> to_double(const std::optional<std::string>& text) {
    if (!text) {
        return std::nullopt;
    }
    double value = 0;
    const char* begin = text->data();
    auto [ptr, ec] = std::from_chars(begin, begin + text->size(), value);
    if (ec != std::errc{} || ptr != begin + text->size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::uint64_t> to_u64(const std::optional<std::string>& text) {
    if (!text) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    const char* begin = text->data();
    auto [ptr, ec] = std::from_chars(begin, begin + text->size(), value);
    if (ec != std::errc{} || ptr != begin + text->size()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

Result<Budget> load_budget(const std::string& path) {
    auto yaml = load_yaml_file(path);
    if (!yaml) {
        return Result<Budget>::err(yaml.error());
    }
    const YamlValue& root = yaml.value();
    Budget budget;
    auto buffer = to_u64(root.scalar("buffer_kb"));
    auto overhead = to_double(root.scalar("max_target_overhead_pct"));
    if (!buffer || !overhead) {
        return Result<Budget>::err(make_error("BUDGET_SCHEMA", path));
    }
    budget.total_buffer_kb = *buffer;
    budget.target_overhead_pct = *overhead;
    budget.max_trace_mb_per_sec = to_double(root.scalar("max_trace_mb_per_sec"));
    if (const YamlValue* weights = root.child("weights")) {
        if (auto weight = to_double(weights->scalar("trace_rate"))) {
            budget.weight_trace_rate = *weight;
        }
        if (auto weight = to_double(weights->scalar("overhead"))) {
            budget.weight_overhead = *weight;
        }
    }
    if (auto floor = to_double(root.scalar("critical_buffer_floor"))) {
        budget.critical_buffer_floor = *floor;
    }
    return Result<Budget>::ok(budget);
}

} // namespace tracearbiter
