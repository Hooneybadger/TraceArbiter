#include "tracearbiter/smoke.hpp"
#include "tracearbiter/tracefs.hpp"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <optional>
#include <thread>
#include <unistd.h>
#include <vector>

namespace tracearbiter {
namespace {

void generate_switches(std::chrono::milliseconds duration) {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    std::vector<std::thread> workers;
    workers.reserve(4);
    for (int i = 0; i < 4; ++i) {
        workers.emplace_back([deadline] {
            while (std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
}

void put_u64(Json& object, const char* key, std::optional<std::uint64_t> value) {
    if (value) {
        object[key] = Json::integer(static_cast<std::int64_t>(*value));
    } else {
        object[key] = Json::null();
    }
}

} // namespace

SmokeResult run_smoke(const std::filesystem::path& output, const std::string& event_system,
                      const std::string& event_name) {
    SmokeResult result;
    Json json = Json::object();
    json["event"] = Json::string(event_system + ":" + event_name);

    auto opened = Tracefs::open();
    if (!opened) {
        json["status"] = Json::string("BLOCKED");
        json["reason"] = Json::string(opened.error().code);
        json["message"] = Json::string(opened.error().message);
        result.json = std::move(json);
        return result;
    }
    Tracefs& fs = opened.value();
    json["tracefs_root"] = Json::string(fs.root().string());
    json["readable"] = Json::boolean(fs.readable());
    json["writable"] = Json::boolean(fs.writable());
    if (!fs.readable() || !fs.writable()) {
        json["status"] = Json::string("BLOCKED");
        json["reason"] = Json::string(fs.block_reason());
        result.json = std::move(json);
        return result;
    }
    if (!fs.event_exists(event_system, event_name)) {
        json["status"] = Json::string("BLOCKED");
        json["reason"] = Json::string("EVENT_MISSING");
        result.json = std::move(json);
        return result;
    }

    const std::string name = "ta_smoke_" + std::to_string(::getpid());
    json["instance"] = Json::string(name);
    std::filesystem::path instance_path;
    BufferStats stats;
    std::optional<std::uint64_t> buffer_kb;
    std::optional<Error> runtime_error;

    {
        auto created = fs.create_instance(name);
        if (!created) {
            json["status"] = Json::string("BLOCKED");
            json["reason"] = Json::string(created.error().code);
            json["message"] = Json::string(created.error().message);
            result.json = std::move(json);
            return result;
        }
        Tracefs::Instance& instance = created.value();
        instance_path = instance.path();
        if (auto err = instance.set_buffer_size_kb(1408)) {
            runtime_error = *err;
        } else if (auto err = instance.enable_event(event_system, event_name, true)) {
            runtime_error = *err;
        } else if (auto err = instance.set_tracing_on(true)) {
            runtime_error = *err;
        } else {
            generate_switches(std::chrono::milliseconds(250));
            instance.set_tracing_on(false);
            stats = instance.read_stats();
            buffer_kb = instance.buffer_size_kb();
        }
    }

    json["instance_removed"] = Json::boolean(!std::filesystem::exists(instance_path));
    if (runtime_error) {
        json["status"] = Json::string("ERROR");
        json["reason"] = Json::string(runtime_error->code);
        json["message"] = Json::string(runtime_error->message);
        result.json = std::move(json);
        std::filesystem::create_directories(output.parent_path());
        std::ofstream out(output);
        out << result.json.dump(2);
        return result;
    }

    json["stats"] = Json::object();
    put_u64(json["stats"], "entries", stats.entries);
    put_u64(json["stats"], "overrun", stats.overrun);
    put_u64(json["stats"], "bytes", stats.bytes);
    put_u64(json["stats"], "dropped_events", stats.dropped_events);
    put_u64(json["stats"], "read_events", stats.read_events);
    put_u64(json, "buffer_kb", buffer_kb);

    if (stats.entries && *stats.entries > 0) {
        json["status"] = Json::string("OK");
        result.exit_code = 0;
    } else {
        json["status"] = Json::string("NO_EVENTS");
        json["reason"] = Json::string("NO_EVENTS_CAPTURED");
        result.exit_code = 1;
    }
    result.json = json;
    std::filesystem::create_directories(output.parent_path());
    std::ofstream out(output);
    out << result.json.dump(2);
    return result;
}

} // namespace tracearbiter
