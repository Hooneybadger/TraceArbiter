#pragma once

#include "tracearbiter/json.hpp"

#include <filesystem>
#include <string>

namespace tracearbiter {

Json collect_preflight(const std::filesystem::path& repo_root);
std::string write_preflight(const std::filesystem::path& output,
                            const std::filesystem::path& repo_root);

} // namespace tracearbiter
