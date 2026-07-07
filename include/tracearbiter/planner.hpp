#pragma once

#include "tracearbiter/budget.hpp"
#include "tracearbiter/plan.hpp"
#include "tracearbiter/source.hpp"

#include <vector>

namespace tracearbiter {

TracePlan compose_plan(const std::vector<TraceSource>& sources, const Budget& budget);

} // namespace tracearbiter
