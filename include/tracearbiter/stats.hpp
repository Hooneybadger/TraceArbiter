#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tracearbiter {

struct BufferStats {
    std::optional<std::uint64_t> entries;
    std::optional<std::uint64_t> overrun;
    std::optional<std::uint64_t> commit_overrun;
    std::optional<std::uint64_t> bytes;
    std::optional<std::uint64_t> dropped_events;
    std::optional<std::uint64_t> read_events;
};

BufferStats parse_buffer_stats(std::string_view text);
BufferStats add_stats(BufferStats left, const BufferStats& right);

} // namespace tracearbiter
