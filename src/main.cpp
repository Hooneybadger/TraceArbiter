#include "tracearbiter/budget.hpp"
#include "tracearbiter/plan.hpp"
#include "tracearbiter/planner.hpp"
#include "tracearbiter/preflight.hpp"
#include "tracearbiter/session.hpp"
#include "tracearbiter/smoke.hpp"
#include "tracearbiter/source.hpp"
#include "tracearbiter/tracefs.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

int usage() {
    std::cerr << "usage: tracearbiter preflight [--output PATH]\n"
              << "       tracearbiter discover [--json]\n"
              << "       tracearbiter plan --priorities FILE --budget FILE [--baseline b1|b2|b3|ref]\n"
              << "       tracearbiter smoke [--output PATH]\n"
              << "       tracearbiter exec --output PATH [--plan FILE|--no-trace]\n"
              << "                 [--workdir DIR] [--accept-exit N,N] -- CMD...\n";
    return 2;
}

std::filesystem::path repo_root() {
    auto cwd = std::filesystem::current_path();
    if (std::filesystem::exists(cwd / "configs/priorities.yaml")) {
        return cwd;
    }
    if (std::filesystem::exists(cwd.parent_path() / "configs/priorities.yaml")) {
        return cwd.parent_path();
    }
    return cwd;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        return usage();
    }
    const std::string command = argv[1];
    if (command == "preflight") {
        std::filesystem::path output = "artifacts/preflight/system.json";
        for (int i = 2; i < argc; ++i) {
            if (std::string(argv[i]) == "--output" && i + 1 < argc) {
                output = argv[++i];
            }
        }
        const auto dumped = tracearbiter::write_preflight(output, repo_root());
        std::cout << dumped;
        return dumped.find("\"status\": \"BLOCKED\"") == std::string::npos ? 0 : 1;
    }
    if (command == "discover") {
        auto fs = tracearbiter::Tracefs::open();
        if (!fs) {
            std::cerr << fs.error().code << " " << fs.error().message << "\n";
            return 1;
        }
        if (!fs.value().readable()) {
            std::cerr << fs.value().block_reason() << "\n" << std::flush;
            return 1;
        }
        for (const auto& event : fs.value().available_events()) {
            std::cout << event << "\n";
        }
        return 0;
    }
    if (command == "plan") {
        std::string priorities_path;
        std::string budget_path;
        std::string baseline = "b3";
        std::uint64_t ref_buffer = 65536;
        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--priorities" && i + 1 < argc) {
                priorities_path = argv[++i];
            } else if (arg == "--budget" && i + 1 < argc) {
                budget_path = argv[++i];
            } else if (arg == "--baseline" && i + 1 < argc) {
                baseline = argv[++i];
            } else if (arg == "--ref-buffer-kb" && i + 1 < argc) {
                ref_buffer = std::strtoull(argv[++i], nullptr, 10);
            }
        }
        if (priorities_path.empty()) {
            priorities_path = (repo_root() / "configs/priorities.yaml").string();
        }
        if (budget_path.empty()) {
            budget_path = (repo_root() / "configs/budgets/small.yaml").string();
        }
        auto sources = tracearbiter::load_priorities(priorities_path);
        auto budget = tracearbiter::load_budget(budget_path);
        if (!sources) {
            std::cerr << sources.error().code << " " << sources.error().message << "\n";
            return 1;
        }
        if (!budget) {
            std::cerr << budget.error().code << " " << budget.error().message << "\n";
            return 1;
        }
        auto fs = tracearbiter::Tracefs::open();
        if (fs && fs.value().readable()) {
            for (auto& source : sources.value()) {
                source.available = fs.value().event_exists(source.system, source.event);
            }
        }
        tracearbiter::TracePlan plan;
        if (baseline == "b1") {
            plan = tracearbiter::full_single_plan(sources.value(), budget.value());
        } else if (baseline == "b2") {
            plan = tracearbiter::static_split_plan(sources.value(), budget.value());
        } else if (baseline == "ref") {
            plan = tracearbiter::critical_only_plan(sources.value(), ref_buffer);
        } else {
            plan = tracearbiter::compose_plan(sources.value(), budget.value());
        }
        std::cout << tracearbiter::plan_to_json(plan).dump(2);
        return plan.feasible ? 0 : 1;
    }
    if (command == "smoke") {
        std::filesystem::path output = "artifacts/smoke/session.json";
        for (int i = 2; i < argc; ++i) {
            if (std::string(argv[i]) == "--output" && i + 1 < argc) {
                output = argv[++i];
            }
        }
        auto result = tracearbiter::run_smoke(output);
        std::cout << result.json.dump(2);
        return result.exit_code;
    }
    if (command == "exec") {
        tracearbiter::ExecRequest request;
        std::string plan_path;
        bool no_trace = false;
        int dash = argc;
        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--") {
                dash = i;
                break;
            }
            if (arg == "--output" && i + 1 < argc) {
                request.output = argv[++i];
            } else if (arg == "--plan" && i + 1 < argc) {
                plan_path = argv[++i];
            } else if (arg == "--workdir" && i + 1 < argc) {
                request.workdir = argv[++i];
            } else if (arg == "--no-trace") {
                no_trace = true;
            } else if (arg == "--accept-exit" && i + 1 < argc) {
                request.accept_exit.clear();
                std::string list = argv[++i];
                std::size_t pos = 0;
                while (pos < list.size()) {
                    auto comma = list.find(',', pos);
                    auto token = list.substr(pos, comma - pos);
                    request.accept_exit.push_back(std::atoi(token.c_str()));
                    if (comma == std::string::npos) {
                        break;
                    }
                    pos = comma + 1;
                }
            } else {
                return usage();
            }
        }
        for (int i = dash + 1; i < argc; ++i) {
            request.command.emplace_back(argv[i]);
        }
        if (request.output.empty() || request.command.empty()) {
            return usage();
        }
        request.trace = !no_trace;
        tracearbiter::TracePlan plan;
        plan.feasible = true;
        if (!no_trace) {
            if (plan_path.empty()) {
                std::cerr << "exec requires --plan or --no-trace\n";
                return 2;
            }
            auto loaded = tracearbiter::load_plan(plan_path);
            if (!loaded) {
                std::cerr << loaded.error().code << " " << loaded.error().message << "\n";
                return 1;
            }
            plan = loaded.value();
        }
        auto result = tracearbiter::run_exec(plan, request);
        std::cout << result.json.dump(2);
        return result.exit_code;
    }
    return usage();
}
