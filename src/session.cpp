#include "tracearbiter/session.hpp"
#include "tracearbiter/manifest.hpp"
#include "tracearbiter/source.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <sys/wait.h>
#include <unistd.h>

namespace tracearbiter {
namespace {

std::uint64_t count_event_lines(const std::filesystem::path& trace, const std::string& event) {
    const std::string needle = ": " + event + ":";
    std::ifstream in(trace);
    if (!in) {
        return 0;
    }
    std::uint64_t count = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.find(needle) != std::string::npos) {
            ++count;
        }
    }
    return count;
}

void put_u64(Json& object, const char* key, std::optional<std::uint64_t> value) {
    if (value) {
        object[key] = Json::integer(static_cast<std::int64_t>(*value));
    } else {
        object[key] = Json::null();
    }
}

int wait_status_code(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

bool accepted(int code, const std::vector<int>& allow) {
    return std::find(allow.begin(), allow.end(), code) != allow.end();
}

int spawn_and_wait(const ExecRequest& request, const std::filesystem::path& stdout_path,
                   const std::filesystem::path& stderr_path) {
    pid_t pid = fork();
    if (pid < 0) {
        return 127;
    }
    if (pid == 0) {
        if (!request.workdir.empty()) {
            if (::chdir(request.workdir.c_str()) != 0) {
                _exit(127);
            }
        }
        int out = ::open(stdout_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        int err = ::open(stderr_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out >= 0) {
            dup2(out, STDOUT_FILENO);
            close(out);
        }
        if (err >= 0) {
            dup2(err, STDERR_FILENO);
            close(err);
        }
        std::vector<char*> argv;
        for (const auto& arg : request.command) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return 127;
    }
    return wait_status_code(status);
}

} // namespace

std::string kernel_instance_name(std::string_view logical) {
    if (logical == "critical") {
        return "ta_crit";
    }
    if (logical == "bulk") {
        return "ta_bulk";
    }
    if (logical == "full") {
        return "ta_full";
    }
    std::string name = "ta_";
    for (char ch : logical) {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') {
            name.push_back(ch);
        }
    }
    if (name.size() <= 3) {
        name += "x";
    }
    return name;
}

ExecResult run_exec(const TracePlan& plan, const ExecRequest& request) {
    ExecResult result;
    Json json = Json::object();
    json["git_commit"] = Json::string(git_commit());
    json["git_dirty"] = Json::boolean(git_dirty());
    json["feasible"] = Json::boolean(plan.feasible);
    json["total_buffer_kb"] = Json::integer(static_cast<std::int64_t>(plan.total_buffer_kb));
    json["command"] = Json::array();
    for (const auto& arg : request.command) {
        json["command"].push(Json::string(arg));
    }

    auto opened = Tracefs::open();
    if (request.trace) {
        if (!opened || !opened.value().readable() || !opened.value().writable()) {
            json["status"] = Json::string("BLOCKED");
            json["reason"] = Json::string(
                !opened ? opened.error().code
                        : opened.value().block_reason());
            result.json = json;
            return result;
        }
    }

    struct Slot {
        Tracefs::Instance instance;
        std::string logical;
        std::string kernel;
        std::uint64_t buffer_kb{0};
        std::vector<SourceId> sources;
    };
    std::vector<Slot> slots;

    auto fail = [&](const std::string& code, const std::string& message) {
        json["status"] = Json::string("ERROR");
        json["reason"] = Json::string(code);
        json["message"] = Json::string(message);
        result.json = json;
        return result;
    };

    if (request.trace && !plan.sources.empty()) {
        Tracefs& fs = opened.value();
        std::map<std::string, std::uint64_t> buffers;
        std::map<std::string, std::vector<SourceId>> grouped;
        for (const auto& row : plan.sources) {
            if (!row.enabled) {
                continue;
            }
            grouped[row.instance].push_back(row.source);
            buffers[row.instance] = row.buffer_kb;
        }
        for (const auto& [logical, sources] : grouped) {
            auto created = fs.create_instance(kernel_instance_name(logical));
            if (!created) {
                return fail(created.error().code, created.error().message);
            }
            Slot slot{std::move(created.value()), logical, kernel_instance_name(logical),
                      buffers[logical], sources};
            if (slot.buffer_kb > 0) {
                if (auto err = slot.instance.set_buffer_size_kb(slot.buffer_kb)) {
                    return fail(err->code, err->message);
                }
            }
            if (auto err = slot.instance.set_tracing_on(false)) {
                return fail(err->code, err->message);
            }
            for (const auto& id : sources) {
                std::string system;
                std::string event;
                if (!split_source_id(id, system, event)) {
                    return fail("SOURCE_ID", id);
                }
                if (auto err = slot.instance.enable_event(system, event, true)) {
                    return fail(err->code, err->message + " " + id);
                }
            }
            slots.push_back(std::move(slot));
        }
        for (auto& slot : slots) {
            if (auto err = slot.instance.set_tracing_on(true)) {
                return fail(err->code, err->message);
            }
        }
    }

    if (request.command.empty()) {
        return fail("COMMAND", "missing command");
    }

    std::filesystem::create_directories(request.output.parent_path());
    auto stdout_path = request.output;
    stdout_path += ".stdout";
    auto stderr_path = request.output;
    stderr_path += ".stderr";

    const auto started = std::chrono::steady_clock::now();
    const int code = spawn_and_wait(request, stdout_path, stderr_path);
    const auto ended = std::chrono::steady_clock::now();
    const double runtime =
        std::chrono::duration<double>(ended - started).count();

    json["runtime_s"] = Json::number(runtime);
    json["exit_code"] = Json::integer(code);
    json["accepted"] = Json::boolean(accepted(code, request.accept_exit));
    json["stdout_path"] = Json::string(stdout_path.string());
    json["stderr_path"] = Json::string(stderr_path.string());

    json["observations"] = Json::array();
    json["critical_event_counts"] = Json::object();
    std::uint64_t critical_total = 0;
    std::uint64_t all_entries = 0;
    std::uint64_t all_overrun = 0;
    std::uint64_t all_bytes = 0;
    std::uint64_t all_dropped = 0;

    for (auto& slot : slots) {
        slot.instance.set_tracing_on(false);
        auto stats = slot.instance.read_stats();
        Json row = Json::object();
        row["instance"] = Json::string(slot.logical);
        row["kernel_instance"] = Json::string(slot.kernel);
        put_u64(row, "buffer_kb", slot.instance.buffer_size_kb());
        put_u64(row, "entries", stats.entries);
        put_u64(row, "overrun", stats.overrun);
        put_u64(row, "commit_overrun", stats.commit_overrun);
        put_u64(row, "bytes", stats.bytes);
        put_u64(row, "dropped_events", stats.dropped_events);
        json["observations"].push(row);
        if (stats.entries) {
            all_entries += *stats.entries;
        }
        if (stats.overrun) {
            all_overrun += *stats.overrun;
        }
        if (stats.bytes) {
            all_bytes += *stats.bytes;
        }
        if (stats.dropped_events) {
            all_dropped += *stats.dropped_events;
        }
        const auto trace_path = slot.instance.path() / "trace";
        for (const auto& id : slot.sources) {
            std::string system;
            std::string event;
            split_source_id(id, system, event);
            auto count = count_event_lines(trace_path, event);
            json["critical_event_counts"][id] =
                Json::integer(static_cast<std::int64_t>(count));
            if (slot.logical == "critical" || slot.logical == "full") {
                // Count scheduler-class events toward retention later in analysis
                // by id; still accumulate a raw total for convenience.
                critical_total += count;
            }
        }
    }
    json["entries"] = Json::integer(static_cast<std::int64_t>(all_entries));
    json["overrun"] = Json::integer(static_cast<std::int64_t>(all_overrun));
    json["bytes"] = Json::integer(static_cast<std::int64_t>(all_bytes));
    json["dropped_events"] = Json::integer(static_cast<std::int64_t>(all_dropped));
    json["trace_event_lines"] = Json::integer(static_cast<std::int64_t>(critical_total));
    json["status"] = Json::string(accepted(code, request.accept_exit) ? "OK" : "WORKLOAD_FAILED");
    result.exit_code = accepted(code, request.accept_exit) ? 0 : 1;
    result.json = json;
    std::ofstream out(request.output);
    out << result.json.dump(2);
    return result;
}

} // namespace tracearbiter
