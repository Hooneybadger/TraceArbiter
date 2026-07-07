#pragma once

#include "tracearbiter/error.hpp"
#include "tracearbiter/json.hpp"
#include "tracearbiter/plan.hpp"
#include "tracearbiter/tracefs.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace tracearbiter {

struct ExecRequest {
    std::filesystem::path output;
    std::filesystem::path workdir;
    std::vector<std::string> command;
    std::vector<int> accept_exit{0};
    bool trace{true};
};

struct ExecResult {
    Json json = Json::object();
    int exit_code{1};
};

std::string kernel_instance_name(std::string_view logical);
ExecResult run_exec(const TracePlan& plan, const ExecRequest& request);

} // namespace tracearbiter
