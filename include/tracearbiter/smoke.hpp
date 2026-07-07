#pragma once

#include "tracearbiter/json.hpp"

#include <filesystem>
#include <string>

namespace tracearbiter {

struct SmokeResult {
    Json json = Json::object();
    int exit_code{1};
};

SmokeResult run_smoke(const std::filesystem::path& output,
                      const std::string& event_system = "sched",
                      const std::string& event_name = "sched_switch");

} // namespace tracearbiter
