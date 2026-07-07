#pragma once

#include "tracearbiter/error.hpp"
#include "tracearbiter/source.hpp"
#include "tracearbiter/stats.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tracearbiter {

class Tracefs {
public:
    static std::filesystem::path default_root();
    static Result<Tracefs> open(std::filesystem::path root = default_root());

    const std::filesystem::path& root() const { return root_; }
    bool readable() const { return readable_; }
    bool writable() const { return writable_; }
    const std::string& block_reason() const { return block_reason_; }

    std::vector<std::string> event_systems() const;
    std::vector<SourceId> available_events() const;
    bool event_exists(std::string_view system, std::string_view event) const;
    std::string current_tracer() const;
    std::string trace_clock() const;
    std::optional<std::uint64_t> buffer_size_kb() const;
    std::vector<std::string> instance_names() const;

    class Instance {
    public:
        Instance(Instance&& other) noexcept;
        Instance& operator=(Instance&& other) noexcept;
        Instance(const Instance&) = delete;
        Instance& operator=(const Instance&) = delete;
        ~Instance();

        const std::string& name() const { return name_; }
        std::filesystem::path path() const;

        std::optional<Error> set_tracing_on(bool on);
        std::optional<Error> enable_event(std::string_view system, std::string_view event, bool on);
        std::optional<Error> set_buffer_size_kb(std::uint64_t kb);
        std::optional<std::uint64_t> buffer_size_kb() const;
        BufferStats read_stats() const;
        void release();

    private:
        friend class Tracefs;
        Instance(Tracefs* fs, std::string name, bool owner);
        void cleanup() noexcept;

        Tracefs* fs_{nullptr};
        std::string name_;
        bool owner_{false};
    };

    Result<Instance> create_instance(std::string name);

private:
    explicit Tracefs(std::filesystem::path root);
    std::string read_trim(const std::filesystem::path& path) const;
    std::optional<Error> write_text(const std::filesystem::path& path, std::string_view text) const;

    std::filesystem::path root_;
    bool readable_{false};
    bool writable_{false};
    std::string block_reason_;
};

} // namespace tracearbiter
