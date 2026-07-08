#include "tracearbiter/budget.hpp"
#include "tracearbiter/source.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            std::cerr << __FILE__ << ":" << __LINE__ << " CHECK failed: " #cond "\n";                \
            return 1;                                                                                \
        }                                                                                            \
    } while (0)

int main() {
    auto dir = std::filesystem::temp_directory_path() / "tracearbiter-budget-test";
    std::filesystem::create_directories(dir);
    auto path = dir / "budget.yaml";
    {
        std::ofstream out(path);
        out << "buffer_kb: 8192\n"
            << "max_target_overhead_pct: 2.5\n"
            << "max_trace_mb_per_sec: 12\n"
            << "critical_buffer_floor: 0.4\n"
            << "weights:\n"
            << "  trace_rate: 2.0\n"
            << "  overhead: 0.5\n";
    }
    auto budget = tracearbiter::load_budget(path.string());
    CHECK(budget);
    CHECK(budget.value().total_buffer_kb == 8192);
    CHECK(budget.value().target_overhead_pct == 2.5);
    CHECK(budget.value().max_trace_mb_per_sec.has_value());
    CHECK(*budget.value().max_trace_mb_per_sec == 12.0);
    CHECK(budget.value().weight_trace_rate == 2.0);
    CHECK(budget.value().weight_overhead == 0.5);
    CHECK(budget.value().critical_buffer_floor == 0.4);

    auto missing = tracearbiter::load_budget((dir / "nope.yaml").string());
    CHECK(!missing);
    CHECK(missing.error().code == "YAML_OPEN");

    auto priorities_path = dir / "priorities.yaml";
    {
        std::ofstream out(priorities_path);
        out << "sources:\n"
            << "  \"sched:sched_switch\":\n"
            << "    priority: critical\n"
            << "  \"raw_syscalls:sys_enter\":\n"
            << "    priority: bulk\n";
    }
    auto sources = tracearbiter::load_priorities(priorities_path.string());
    CHECK(sources);
    CHECK(sources.value().size() == 2);
    bool saw_switch = false;
    bool saw_syscall = false;
    for (const auto& source : sources.value()) {
        if (source.id() == "sched:sched_switch") {
            saw_switch = true;
            CHECK(source.priority == tracearbiter::Priority::Critical);
        }
        if (source.id() == "raw_syscalls:sys_enter") {
            saw_syscall = true;
            CHECK(source.priority == tracearbiter::Priority::Bulk);
        }
    }
    CHECK(saw_switch);
    CHECK(saw_syscall);

    std::filesystem::remove_all(dir);
    return 0;
}
