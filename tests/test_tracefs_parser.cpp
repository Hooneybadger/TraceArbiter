#include "tracearbiter/stats.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            std::cerr << __FILE__ << ":" << __LINE__ << " CHECK failed: " #cond "\n";                \
            return 1;                                                                                \
        }                                                                                            \
    } while (0)

int main() {
    const char* text = "entries: 128\n"
                       "overrun: 3\n"
                       "commit overrun: 1\n"
                       "bytes: 4096\n"
                       "oldest event ts: 0\n"
                       "now ts: 10\n"
                       "dropped events: 2\n"
                       "read events: 100\n"
                       "ignored: not-a-number\n";
    auto stats = tracearbiter::parse_buffer_stats(text);
    CHECK(stats.entries && *stats.entries == 128);
    CHECK(stats.overrun && *stats.overrun == 3);
    CHECK(stats.commit_overrun && *stats.commit_overrun == 1);
    CHECK(stats.bytes && *stats.bytes == 4096);
    CHECK(stats.dropped_events && *stats.dropped_events == 2);
    CHECK(stats.read_events && *stats.read_events == 100);

    std::ifstream in("tests/fixtures/cpu0.stats");
    CHECK(static_cast<bool>(in));
    std::ostringstream buffer;
    buffer << in.rdbuf();
    auto from_file = tracearbiter::parse_buffer_stats(buffer.str());
    CHECK(from_file.entries && *from_file.entries == 128);
    CHECK(from_file.overrun && *from_file.overrun == 3);

    auto summed = tracearbiter::add_stats(stats, from_file);
    CHECK(summed.entries && *summed.entries == 256);
    CHECK(summed.overrun && *summed.overrun == 6);
    CHECK(summed.bytes && *summed.bytes == 8192);

    auto empty = tracearbiter::parse_buffer_stats("not a stats file\n");
    CHECK(!empty.entries);
    CHECK(!empty.overrun);
    return 0;
}
