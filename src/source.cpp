#include "tracearbiter/source.hpp"
#include "tracearbiter/yaml.hpp"

namespace tracearbiter {
namespace {

std::optional<double> to_double(const std::optional<std::string>& text) {
    if (!text) {
        return std::nullopt;
    }
    try {
        std::size_t idx = 0;
        double value = std::stod(*text, &idx);
        if (idx != text->size()) {
            return std::nullopt;
        }
        return value;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace

Result<std::vector<TraceSource>> load_priorities(const std::string& path) {
    auto yaml = load_yaml_file(path);
    if (!yaml) {
        return Result<std::vector<TraceSource>>::err(yaml.error());
    }
    const YamlValue* sources = yaml.value().child("sources");
    if (!sources || !sources->as_map()) {
        return Result<std::vector<TraceSource>>::err(
            make_error("PRIORITY_SCHEMA", "missing sources map"));
    }
    std::vector<TraceSource> out;
    for (const auto& [id, node] : *sources->as_map()) {
        auto colon = id.find(':');
        if (colon == std::string::npos) {
            return Result<std::vector<TraceSource>>::err(make_error("SOURCE_ID", id));
        }
        auto priority_text = node.scalar("priority");
        if (!priority_text) {
            return Result<std::vector<TraceSource>>::err(
                make_error("PRIORITY_MISSING", id));
        }
        auto priority = parse_priority(*priority_text);
        if (!priority) {
            return Result<std::vector<TraceSource>>::err(priority.error());
        }
        TraceSource source;
        source.system = id.substr(0, colon);
        source.event = id.substr(colon + 1);
        source.priority = priority.value();
        source.measured_events_per_sec = to_double(node.scalar("measured_events_per_sec"));
        source.measured_bytes_per_sec = to_double(node.scalar("measured_bytes_per_sec"));
        source.measured_overhead_pct = to_double(node.scalar("measured_overhead_pct"));
        if (auto available = node.scalar("available")) {
            source.available = *available == "true" || *available == "1";
        }
        out.push_back(std::move(source));
    }
    return Result<std::vector<TraceSource>>::ok(std::move(out));
}

} // namespace tracearbiter
