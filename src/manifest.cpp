#include "tracearbiter/manifest.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <string>

namespace tracearbiter {
namespace {

std::string run_git(const char* args) {
    std::string command = std::string("git ") + args + " 2>/dev/null";
    std::unique_ptr<FILE, int (*)(FILE*)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) {
        return {};
    }
    std::array<char, 256> buffer{};
    std::string out;
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        out += buffer.data();
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
        out.pop_back();
    }
    return out;
}

} // namespace

std::string git_commit() { return run_git("rev-parse HEAD"); }

bool git_dirty() {
    const std::string status = run_git("status --porcelain");
    return !status.empty();
}

} // namespace tracearbiter
