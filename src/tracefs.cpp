#include "tracearbiter/tracefs.hpp"

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unistd.h>
#include <system_error>
#include <vector>

namespace tracearbiter {
namespace {

std::mutex g_owned_mu;
std::vector<std::filesystem::path> g_owned_instances;

void forget_owned(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(g_owned_mu);
    g_owned_instances.erase(std::remove(g_owned_instances.begin(), g_owned_instances.end(), path),
                            g_owned_instances.end());
}

void remember_owned(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(g_owned_mu);
    g_owned_instances.push_back(path);
}

void emergency_stop(int signo) {
    std::vector<std::filesystem::path> paths;
    {
        std::lock_guard<std::mutex> lock(g_owned_mu);
        paths = g_owned_instances;
    }
    for (const auto& path : paths) {
        std::ofstream on(path / "tracing_on", std::ios::trunc);
        if (on) {
            on << "0\n";
        }
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    std::_Exit(128 + signo);
}

void install_signal_cleanup() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        std::signal(SIGINT, emergency_stop);
        std::signal(SIGTERM, emergency_stop);
    });
}

std::optional<std::uint64_t> parse_size_kb(std::string_view text) {
    std::string copy(text);
    auto expanded = copy.find("expanded:");
    if (expanded != std::string::npos) {
        copy = copy.substr(expanded + 9);
    }
    auto begin = copy.find_first_not_of(" \t(");
    if (begin == std::string::npos) {
        return std::nullopt;
    }
    copy = copy.substr(begin);
    try {
        std::size_t idx = 0;
        auto value = std::stoull(copy, &idx);
        return value;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool exists_nothrow(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

bool is_dir_nothrow(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

bool can_read(const std::filesystem::path& path) {
    std::error_code ec;
    auto status = std::filesystem::status(path, ec);
    if (ec || !std::filesystem::exists(status)) {
        return false;
    }
    std::ifstream in(path);
    return static_cast<bool>(in);
}

std::string slurp(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string trim_copy(std::string text) {
    auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

} // namespace

std::filesystem::path Tracefs::default_root() {
    const std::filesystem::path tracing{"/sys/kernel/tracing"};
    std::error_code ec;
    if (std::filesystem::exists(tracing, ec)) {
        return tracing;
    }
    return {"/sys/kernel/debug/tracing"};
}

Tracefs::Tracefs(std::filesystem::path root) : root_(std::move(root)) {
    readable_ = can_read(root_ / "tracing_on") || can_read(root_ / "available_events") ||
                is_dir_nothrow(root_ / "events");
    const auto instances = root_ / "instances";
    writable_ = is_dir_nothrow(instances) && ::access(instances.c_str(), W_OK) == 0;
    if (!readable_) {
        block_reason_ = "BLOCKED_TRACEFS_PERMISSION";
    } else if (!writable_) {
        block_reason_ = "BLOCKED_TRACEFS_PERMISSION";
    }
}

Result<Tracefs> Tracefs::open(std::filesystem::path root) {
    Tracefs fs(std::move(root));
    if (!fs.readable_ && !exists_nothrow(fs.root_)) {
        return Result<Tracefs>::err(make_error("TRACEFS_MISSING", fs.root_.string()));
    }
    return Result<Tracefs>::ok(std::move(fs));
}

std::string Tracefs::read_trim(const std::filesystem::path& path) const {
    return trim_copy(slurp(path));
}

std::optional<Error> Tracefs::write_text(const std::filesystem::path& path, std::string_view text) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return make_error("TRACEFS_WRITE", path.string());
    }
    out << text;
    if (!out) {
        return make_error("TRACEFS_WRITE", path.string());
    }
    return std::nullopt;
}

std::vector<std::string> Tracefs::event_systems() const {
    std::vector<std::string> systems;
    std::error_code ec;
    const auto events = root_ / "events";
    if (!is_dir_nothrow(events)) {
        return systems;
    }
    for (const auto& entry : std::filesystem::directory_iterator(events, ec)) {
        std::error_code dir_ec;
        if (entry.is_directory(dir_ec) && !dir_ec && entry.path().filename() != "enable" &&
            entry.path().filename() != "header_event" && entry.path().filename() != "header_page") {
            systems.push_back(entry.path().filename().string());
        }
    }
    std::sort(systems.begin(), systems.end());
    return systems;
}

std::vector<SourceId> Tracefs::available_events() const {
    std::vector<SourceId> ids;
    const std::string listed = slurp(root_ / "available_events");
    if (!listed.empty()) {
        std::istringstream stream(listed);
        std::string line;
        while (std::getline(stream, line)) {
            line = trim_copy(line);
            if (!line.empty()) {
                ids.push_back(line);
            }
        }
        return ids;
    }
    std::error_code ec;
    const auto events = root_ / "events";
    if (!is_dir_nothrow(events)) {
        return ids;
    }
    for (const auto& system : std::filesystem::directory_iterator(events, ec)) {
        std::error_code dir_ec;
        if (!system.is_directory(dir_ec) || dir_ec) {
            continue;
        }
        for (const auto& event : std::filesystem::directory_iterator(system.path(), ec)) {
            if (event.is_directory(dir_ec) && !dir_ec && exists_nothrow(event.path() / "enable")) {
                ids.push_back(make_source_id(system.path().filename().string(),
                                            event.path().filename().string()));
            }
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool Tracefs::event_exists(std::string_view system, std::string_view event) const {
    return exists_nothrow(root_ / "events" / std::string(system) / std::string(event) / "enable");
}

std::string Tracefs::current_tracer() const { return read_trim(root_ / "current_tracer"); }
std::string Tracefs::trace_clock() const { return read_trim(root_ / "trace_clock"); }

std::optional<std::uint64_t> Tracefs::buffer_size_kb() const {
    const std::string text = read_trim(root_ / "buffer_size_kb");
    if (text.empty()) {
        return std::nullopt;
    }
    return parse_size_kb(text);
}

std::vector<std::string> Tracefs::instance_names() const {
    std::vector<std::string> names;
    std::error_code ec;
    const auto instances = root_ / "instances";
    if (!is_dir_nothrow(instances)) {
        return names;
    }
    for (const auto& entry : std::filesystem::directory_iterator(instances, ec)) {
        std::error_code dir_ec;
        if (entry.is_directory(dir_ec) && !dir_ec) {
            names.push_back(entry.path().filename().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

Tracefs::Instance::Instance(Tracefs* fs, std::string name, bool owner)
    : fs_(fs), name_(std::move(name)), owner_(owner) {
    if (owner_ && fs_) {
        install_signal_cleanup();
        remember_owned(path());
    }
}

Tracefs::Instance::Instance(Instance&& other) noexcept
    : fs_(other.fs_), name_(std::move(other.name_)), owner_(other.owner_) {
    other.fs_ = nullptr;
    other.owner_ = false;
}

Tracefs::Instance& Tracefs::Instance::operator=(Instance&& other) noexcept {
    if (this != &other) {
        cleanup();
        fs_ = other.fs_;
        name_ = std::move(other.name_);
        owner_ = other.owner_;
        other.fs_ = nullptr;
        other.owner_ = false;
    }
    return *this;
}

Tracefs::Instance::~Instance() { cleanup(); }

std::filesystem::path Tracefs::Instance::path() const { return fs_->root_ / "instances" / name_; }

std::optional<Error> Tracefs::Instance::set_tracing_on(bool on) {
    return fs_->write_text(path() / "tracing_on", on ? "1" : "0");
}

std::optional<Error> Tracefs::Instance::enable_event(std::string_view system, std::string_view event, bool on) {
    auto event_path = path() / "events" / std::string(system) / std::string(event) / "enable";
    return fs_->write_text(event_path, on ? "1" : "0");
}

std::optional<Error> Tracefs::Instance::set_buffer_size_kb(std::uint64_t kb) {
    return fs_->write_text(path() / "buffer_size_kb", std::to_string(kb));
}

std::optional<std::uint64_t> Tracefs::Instance::buffer_size_kb() const {
    const std::string text = fs_->read_trim(path() / "buffer_size_kb");
    if (text.empty()) {
        return std::nullopt;
    }
    return parse_size_kb(text);
}

BufferStats Tracefs::Instance::read_stats() const {
    BufferStats total;
    std::error_code ec;
    const auto per_cpu = path() / "per_cpu";
    if (is_dir_nothrow(per_cpu)) {
        for (const auto& cpu : std::filesystem::directory_iterator(per_cpu, ec)) {
            total = add_stats(total, parse_buffer_stats(slurp(cpu.path() / "stats")));
        }
        return total;
    }
    return parse_buffer_stats(slurp(path() / "trace_stat"));
}

void Tracefs::Instance::release() {
    if (owner_ && fs_) {
        forget_owned(path());
    }
    owner_ = false;
}

void Tracefs::Instance::cleanup() noexcept {
    if (!fs_ || !owner_) {
        return;
    }
    auto instance = path();
    forget_owned(instance);
    std::error_code ec;
    fs_->write_text(instance / "tracing_on", "0");
    fs_->write_text(instance / "events/enable", "0");
    std::filesystem::remove(instance, ec);
    if (exists_nothrow(instance)) {
        std::filesystem::remove_all(instance, ec);
    }
    owner_ = false;
}

Result<Tracefs::Instance> Tracefs::create_instance(std::string name) {
    if (!writable_) {
        return Result<Instance>::err(make_error(block_reason_.empty() ? "BLOCKED_TRACEFS_PERMISSION"
                                                                      : block_reason_,
                                                root_.string()));
    }
    auto instance = root_ / "instances" / name;
    std::error_code ec;
    if (exists_nothrow(instance)) {
        write_text(instance / "tracing_on", "0");
        std::filesystem::remove(instance, ec);
        if (exists_nothrow(instance)) {
            std::filesystem::remove_all(instance, ec);
        }
    }
    if (!std::filesystem::create_directory(instance, ec) && ec) {
        return Result<Instance>::err(make_error("INSTANCE_CREATE", instance.string() + " " + ec.message()));
    }
    if (!exists_nothrow(instance / "events") && exists_nothrow(root_ / "events")) {
        std::filesystem::copy(root_ / "events", instance / "events",
                              std::filesystem::copy_options::recursive, ec);
    }
    if (!exists_nothrow(instance / "tracing_on")) {
        write_text(instance / "tracing_on", "0");
    }
    if (!exists_nothrow(instance / "buffer_size_kb") && exists_nothrow(root_ / "buffer_size_kb")) {
        write_text(instance / "buffer_size_kb", read_trim(root_ / "buffer_size_kb"));
    }
    return Result<Instance>::ok(Instance(this, std::move(name), true));
}

} // namespace tracearbiter
