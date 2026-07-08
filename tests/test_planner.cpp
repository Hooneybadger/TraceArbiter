#include "tracearbiter/budget.hpp"
#include "tracearbiter/plan.hpp"
#include "tracearbiter/planner.hpp"
#include "tracearbiter/source.hpp"

#include <iostream>

#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            std::cerr << __FILE__ << ":" << __LINE__ << " CHECK failed: " #cond "\n";                \
            return 1;                                                                                \
        }                                                                                            \
    } while (0)

static tracearbiter::TraceSource src(std::string system, std::string event,
                                     tracearbiter::Priority priority, double bytes, double overhead,
                                     bool available = true) {
    tracearbiter::TraceSource source;
    source.system = std::move(system);
    source.event = std::move(event);
    source.priority = priority;
    source.measured_bytes_per_sec = bytes;
    source.measured_overhead_pct = overhead;
    source.available = available;
    return source;
}

int main() {
    tracearbiter::Budget budget;
    budget.total_buffer_kb = 1000;
    budget.target_overhead_pct = 3.0;
    budget.max_trace_mb_per_sec = 100;
    budget.weight_trace_rate = 1.0;
    budget.weight_overhead = 1.0;
    budget.critical_buffer_floor = 0.5;

    auto critical = src("sched", "sched_switch", tracearbiter::Priority::Critical, 100, 0.5);
    auto useful_cheap = src("sched", "sched_wakeup", tracearbiter::Priority::Useful, 10, 0.2);
    auto useful_costly = src("exceptions", "page_fault_user", tracearbiter::Priority::Useful, 50, 1.0);
    auto bulk = src("raw_syscalls", "sys_enter", tracearbiter::Priority::Bulk, 800, 2.0);
    auto missing = src("sched", "sched_waking", tracearbiter::Priority::Critical, 0, 0, false);

    auto plan = tracearbiter::compose_plan({critical, useful_cheap, useful_costly, bulk, missing},
                                           budget);
    CHECK(plan.feasible);
    CHECK(plan.total_buffer_kb == 1000);
    bool has_switch = false;
    bool has_wakeup = false;
    bool has_fault = false;
    bool has_syscall = false;
    bool has_waking = false;
    for (const auto& row : plan.sources) {
        has_switch |= row.source == "sched:sched_switch";
        has_wakeup |= row.source == "sched:sched_wakeup";
        has_fault |= row.source == "exceptions:page_fault_user";
        has_syscall |= row.source == "raw_syscalls:sys_enter";
        has_waking |= row.source == "sched:sched_waking";
        if (row.source == "sched:sched_switch") {
            CHECK(row.instance == "critical");
            CHECK(row.buffer_kb >= 500);
        }
    }
    CHECK(has_switch);
    CHECK(has_wakeup);
    CHECK(has_fault);
    CHECK(!has_syscall);
    CHECK(!has_waking);

    auto again = tracearbiter::compose_plan({critical, useful_cheap, useful_costly, bulk, missing},
                                            budget);
    CHECK(again.sources.size() == plan.sources.size());
    for (std::size_t i = 0; i < plan.sources.size(); ++i) {
        CHECK(again.sources[i].source == plan.sources[i].source);
        CHECK(again.sources[i].buffer_kb == plan.sources[i].buffer_kb);
    }

    tracearbiter::Budget tight = budget;
    tight.target_overhead_pct = 0.1;
    auto infeasible = tracearbiter::compose_plan({critical, bulk}, tight);
    CHECK(!infeasible.feasible);
    CHECK(!infeasible.reasons.empty());
    CHECK(infeasible.reasons[0] == "CRITICAL_SOURCES_EXCEED_BUDGET" ||
          infeasible.reasons.back() == "CRITICAL_SOURCES_EXCEED_BUDGET");
    bool still_critical = false;
    for (const auto& row : infeasible.sources) {
        still_critical |= row.source == "sched:sched_switch";
        CHECK(row.source != "raw_syscalls:sys_enter");
    }
    CHECK(still_critical);

    tracearbiter::TraceSource unmeasured;
    unmeasured.system = "sched";
    unmeasured.event = "sched_switch";
    unmeasured.priority = tracearbiter::Priority::Critical;
    unmeasured.available = true;
    auto incomplete = tracearbiter::compose_plan({unmeasured}, budget);
    bool saw_incomplete = false;
    for (const auto& reason : incomplete.reasons) {
        saw_incomplete |= reason == "INCOMPLETE_CALIBRATION";
    }
    CHECK(saw_incomplete);

    auto full = tracearbiter::full_single_plan({critical, useful_cheap, bulk}, budget);
    CHECK(full.feasible);
    bool saw_full = false;
    bool bulk_in_full = false;
    for (const auto& row : full.sources) {
        saw_full |= row.instance == "full";
        bulk_in_full |= row.source == "raw_syscalls:sys_enter";
        CHECK(row.buffer_kb == budget.total_buffer_kb);
    }
    CHECK(saw_full);
    CHECK(bulk_in_full);

    auto split = tracearbiter::static_split_plan({critical, useful_cheap, bulk}, budget);
    CHECK(split.feasible);
    bool crit_inst = false;
    bool bulk_inst = false;
    for (const auto& row : split.sources) {
        if (row.source == "sched:sched_switch") {
            CHECK(row.instance == "critical");
            crit_inst = true;
        }
        if (row.source == "raw_syscalls:sys_enter") {
            CHECK(row.instance == "bulk");
            bulk_inst = true;
        }
    }
    CHECK(crit_inst);
    CHECK(bulk_inst);
    return 0;
}
