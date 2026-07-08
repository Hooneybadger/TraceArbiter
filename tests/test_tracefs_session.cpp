#include "tracearbiter/tracefs.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            std::cerr << __FILE__ << ":" << __LINE__ << " CHECK failed: " #cond "\n";                \
            return 1;                                                                                \
        }                                                                                            \
    } while (0)

namespace {

void write_file(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << text;
}

std::string slurp(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "tracearbiter-fake-tracefs";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "instances");
    write_file(root / "tracing_on", "0\n");
    write_file(root / "current_tracer", "nop\n");
    write_file(root / "trace_clock", "local\n");
    write_file(root / "buffer_size_kb", "1408\n");
    write_file(root / "available_events", "sched:sched_switch\nsched:sched_waking\n");
    write_file(root / "events/sched/sched_switch/enable", "0\n");
    write_file(root / "events/sched/sched_waking/enable", "0\n");

    auto opened = tracearbiter::Tracefs::open(root);
    CHECK(opened);
    auto& fs = opened.value();
    CHECK(fs.readable());
    CHECK(fs.writable());
    CHECK(fs.current_tracer() == "nop");
    CHECK(fs.trace_clock() == "local");
    CHECK(fs.buffer_size_kb() && *fs.buffer_size_kb() == 1408);
    CHECK(fs.event_exists("sched", "sched_switch"));
    auto events = fs.available_events();
    CHECK(events.size() >= 2);

    const std::string name = "ta_week1";
    const auto instance_path = root / "instances" / name;
    {
        auto created = fs.create_instance(name);
        CHECK(created);
        auto& instance = created.value();
        CHECK(instance.set_tracing_on(true) == std::nullopt);
        CHECK(instance.enable_event("sched", "sched_switch", true) == std::nullopt);
        CHECK(instance.set_buffer_size_kb(2048) == std::nullopt);
        CHECK(instance.buffer_size_kb() && *instance.buffer_size_kb() == 2048);

        write_file(instance.path() / "per_cpu/cpu0/stats",
                   "entries: 12\noverrun: 1\nbytes: 256\ndropped events: 0\nread events: 12\n");
        write_file(instance.path() / "per_cpu/cpu1/stats",
                   "entries: 8\noverrun: 2\nbytes: 128\ndropped events: 1\nread events: 7\n");

        auto stats = instance.read_stats();
        CHECK(stats.entries && *stats.entries == 20);
        CHECK(stats.overrun && *stats.overrun == 3);
        CHECK(stats.bytes && *stats.bytes == 384);
        CHECK(stats.dropped_events && *stats.dropped_events == 1);
        CHECK(stats.read_events && *stats.read_events == 19);

        auto enable = slurp(instance.path() / "events/sched/sched_switch/enable");
        CHECK(enable.find('1') != std::string::npos);
        CHECK(std::filesystem::exists(instance_path));
    }
    CHECK(!std::filesystem::exists(instance_path));
    std::filesystem::remove_all(root);
    return 0;
}
