#pragma once

#include "tracearbiter/json.hpp"
#include "tracearbiter/plan.hpp"
#include "tracearbiter/stats.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace tracearbiter {

struct InstanceObservation {
    std::string instance;
    BufferStats stats;
    std::uint64_t buffer_kb{0};
};

Json observation_to_json(const std::vector<InstanceObservation>& observations);

} // namespace tracearbiter
