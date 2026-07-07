#pragma once

#include "tracearbiter/error.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tracearbiter {

enum class Priority { Critical, Useful, Bulk };

inline std::string to_string(Priority priority) {
    switch (priority) {
    case Priority::Critical:
        return "critical";
    case Priority::Useful:
        return "useful";
    case Priority::Bulk:
        return "bulk";
    }
    return "bulk";
}

inline Result<Priority> parse_priority(std::string_view text) {
    if (text == "critical") {
        return Result<Priority>::ok(Priority::Critical);
    }
    if (text == "useful") {
        return Result<Priority>::ok(Priority::Useful);
    }
    if (text == "bulk") {
        return Result<Priority>::ok(Priority::Bulk);
    }
    return Result<Priority>::err(make_error("UNKNOWN_PRIORITY", std::string(text)));
}

using SourceId = std::string;

inline SourceId make_source_id(std::string_view system, std::string_view event) {
    SourceId id;
    id.reserve(system.size() + 1 + event.size());
    id.append(system);
    id.push_back(':');
    id.append(event);
    return id;
}

struct TraceSource {
    std::string system;
    std::string event;
    Priority priority{Priority::Bulk};
    std::optional<double> measured_events_per_sec;
    std::optional<double> measured_bytes_per_sec;
    std::optional<double> measured_overhead_pct;
    bool available{false};

    SourceId id() const { return make_source_id(system, event); }
};

inline bool split_source_id(std::string_view id, std::string& system, std::string& event) {
    auto colon = id.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 >= id.size()) {
        return false;
    }
    system.assign(id.substr(0, colon));
    event.assign(id.substr(colon + 1));
    return true;
}

Result<std::vector<TraceSource>> load_priorities(const std::string& path);

} // namespace tracearbiter
