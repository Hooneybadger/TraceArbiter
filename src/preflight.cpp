#include "tracearbiter/preflight.hpp"
#include "tracearbiter/manifest.hpp"
#include "tracearbiter/source.hpp"
#include "tracearbiter/tracefs.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>
#include <thread>
#include <unistd.h>

namespace tracearbiter {
namespace {

std::string slurp(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string first_line(const std::string& text) {
    auto end = text.find('\n');
    return end == std::string::npos ? text : text.substr(0, end);
}

std::string cpu_model() {
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("model name", 0) == 0) {
            auto colon = line.find(':');
            if (colon != std::string::npos) {
                auto value = line.substr(colon + 1);
                auto start = value.find_first_not_of(" \t");
                return start == std::string::npos ? value : value.substr(start);
            }
        }
    }
    return {};
}

int physical_cpus() {
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    int count = 0;
    while (std::getline(in, line)) {
        if (line.rfind("processor", 0) == 0) {
            ++count;
        }
    }
    return count;
}

int physical_cores() {
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    std::string physical;
    std::string core;
    std::set<std::string> seen;
    auto take = [](const std::string& text) {
        auto colon = text.find(':');
        return colon == std::string::npos ? std::string{} : text.substr(colon + 1);
    };
    auto flush = [&] {
        if (!physical.empty() || !core.empty()) {
            seen.insert(physical + ":" + core);
        }
        physical.clear();
        core.clear();
    };
    while (std::getline(in, line)) {
        if (line.empty()) {
            flush();
            continue;
        }
        if (line.rfind("physical id", 0) == 0) {
            physical = take(line);
        } else if (line.rfind("core id", 0) == 0) {
            core = take(line);
        }
    }
    flush();
    return static_cast<int>(seen.size());
}

std::string smt_status() {
    const std::string active = slurp("/sys/devices/system/cpu/smt/active");
    if (active.empty()) {
        return {};
    }
    return first_line(active) == "1" ? "on" : "off";
}

int numa_nodes() {
    int count = 0;
    std::error_code ec;
    const std::filesystem::path node{"/sys/devices/system/node"};
    if (!std::filesystem::is_directory(node, ec)) {
        return 0;
    }
    for (const auto& entry : std::filesystem::directory_iterator(node, ec)) {
        const auto name = entry.path().filename().string();
        if (name.rfind("node", 0) == 0 && name.size() > 4 && std::isdigit(static_cast<unsigned char>(name[4]))) {
            ++count;
        }
    }
    return count;
}

} // namespace

Json collect_preflight(const std::filesystem::path& repo_root) {
    Json json = Json::object();
    json["kernel"] = Json::string(first_line(slurp("/proc/sys/kernel/osrelease")));
    json["cpu_model"] = Json::string(cpu_model());
    json["logical_cpus"] = Json::integer(static_cast<std::int64_t>(std::thread::hardware_concurrency()));
    json["physical_cpu_records"] = Json::integer(physical_cpus());
    json["physical_cores"] = Json::integer(physical_cores());
    json["smt"] = Json::string(smt_status());
    json["numa_nodes"] = Json::integer(numa_nodes());
    json["compiler"] = Json::string(__VERSION__);
    json["git_commit"] = Json::string(git_commit());
    json["git_dirty"] = Json::boolean(git_dirty());
    json["repo_root"] = Json::string(repo_root.string());

    auto tracing = Tracefs::open();
    if (!tracing) {
        json["tracefs"] = Json::object();
        json["tracefs"]["status"] = Json::string("BLOCKED");
        json["tracefs"]["reason"] = Json::string(tracing.error().code);
        json["tracefs"]["message"] = Json::string(tracing.error().message);
        json["status"] = Json::string("BLOCKED");
        return json;
    }
    const Tracefs& fs = tracing.value();
    json["tracefs"] = Json::object();
    json["tracefs"]["root"] = Json::string(fs.root().string());
    json["tracefs"]["readable"] = Json::boolean(fs.readable());
    json["tracefs"]["writable"] = Json::boolean(fs.writable());
    json["tracefs"]["current_tracer"] = Json::string(fs.current_tracer());
    json["tracefs"]["trace_clock"] = Json::string(fs.trace_clock());
    if (auto kb = fs.buffer_size_kb()) {
        json["tracefs"]["buffer_size_kb"] = Json::integer(static_cast<std::int64_t>(*kb));
    } else {
        json["tracefs"]["buffer_size_kb"] = Json::null();
    }
    json["tracefs"]["event_systems"] = Json::array();
    for (const auto& system : fs.event_systems()) {
        json["tracefs"]["event_systems"].push(Json::string(system));
    }
    json["tracefs"]["instances"] = Json::array();
    for (const auto& name : fs.instance_names()) {
        json["tracefs"]["instances"].push(Json::string(name));
    }
    auto priorities = load_priorities((repo_root / "configs/priorities.yaml").string());
    json["candidate_events"] = Json::array();
    if (priorities) {
        for (auto& source : priorities.value()) {
            source.available = fs.event_exists(source.system, source.event);
            Json row = Json::object();
            row["id"] = Json::string(source.id());
            row["available"] = Json::boolean(source.available);
            row["priority"] = Json::string(to_string(source.priority));
            json["candidate_events"].push(std::move(row));
        }
    }
    if (!fs.readable() || !fs.writable()) {
        json["tracefs"]["reason"] = Json::string(fs.block_reason());
        json["status"] = Json::string("BLOCKED");
    } else {
        json["status"] = Json::string("READY");
    }
    return json;
}

std::string write_preflight(const std::filesystem::path& output,
                            const std::filesystem::path& repo_root) {
    auto json = collect_preflight(repo_root);
    std::filesystem::create_directories(output.parent_path());
    std::ofstream out(output);
    out << json.dump(2);
    return json.dump(2);
}

} // namespace tracearbiter
