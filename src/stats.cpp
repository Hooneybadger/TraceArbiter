#include "tracearbiter/stats.hpp"

#include <charconv>
#include <sstream>
#include <string>

namespace tracearbiter {
namespace {

std::optional<std::uint64_t> parse_u64(std::string_view text) {
    std::uint64_t value = 0;
    auto begin = text.data();
    auto end = text.data() + text.size();
    auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t')) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

void set_field(BufferStats& stats, std::string_view key, std::uint64_t value) {
    if (key == "entries") {
        stats.entries = value;
    } else if (key == "overrun") {
        stats.overrun = value;
    } else if (key == "commit overrun") {
        stats.commit_overrun = value;
    } else if (key == "bytes") {
        stats.bytes = value;
    } else if (key == "dropped events") {
        stats.dropped_events = value;
    } else if (key == "read events") {
        stats.read_events = value;
    }
}

void add_field(std::optional<std::uint64_t>& dst, std::optional<std::uint64_t> src) {
    if (!src) {
        return;
    }
    dst = dst.value_or(0) + *src;
}

} // namespace

BufferStats parse_buffer_stats(std::string_view text) {
    BufferStats stats;
    std::istringstream stream{std::string(text)};
    std::string line;
    while (std::getline(stream, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        auto key = trim(line.substr(0, colon));
        auto value = parse_u64(trim(line.substr(colon + 1)));
        if (value) {
            set_field(stats, key, *value);
        }
    }
    return stats;
}

BufferStats add_stats(BufferStats left, const BufferStats& right) {
    add_field(left.entries, right.entries);
    add_field(left.overrun, right.overrun);
    add_field(left.commit_overrun, right.commit_overrun);
    add_field(left.bytes, right.bytes);
    add_field(left.dropped_events, right.dropped_events);
    add_field(left.read_events, right.read_events);
    return left;
}

} // namespace tracearbiter
