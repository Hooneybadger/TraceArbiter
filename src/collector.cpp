#include "tracearbiter/collector.hpp"

namespace tracearbiter {

Json observation_to_json(const std::vector<InstanceObservation>& observations) {
    Json json = Json::array();
    for (const auto& row : observations) {
        Json item = Json::object();
        item["instance"] = Json::string(row.instance);
        item["buffer_kb"] = Json::integer(static_cast<std::int64_t>(row.buffer_kb));
        auto field = [](Json& object, const char* key, std::optional<std::uint64_t> value) {
            if (value) {
                object[key] = Json::integer(static_cast<std::int64_t>(*value));
            } else {
                object[key] = Json::null();
            }
        };
        field(item, "entries", row.stats.entries);
        field(item, "overrun", row.stats.overrun);
        field(item, "commit_overrun", row.stats.commit_overrun);
        field(item, "bytes", row.stats.bytes);
        field(item, "dropped_events", row.stats.dropped_events);
        field(item, "read_events", row.stats.read_events);
        json.push(std::move(item));
    }
    return json;
}

} // namespace tracearbiter
