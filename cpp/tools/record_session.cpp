// Runs the benchmark workload through the matching engine and records every
// event to a log file. Pair with replay_session to check determinism:
//
//   ./record_session session.bin 1000000
//   ./replay_session session.bin
//
// Both print a summary; the trade fingerprints must match.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "../bench/workload.hpp"
#include "exchange/matching_engine.hpp"
#include "session_summary.hpp"

using namespace exchange;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <log_file> [number_of_events]\n", argv[0]);
        return 1;
    }
    std::string path = argv[1];
    std::size_t count = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1'000'000;

    EventLogWriter writer(path);
    MatchingEngine engine(&writer);
    std::vector<Trade> trades;

    for (const auto& action : bench::make_workload(count)) {
        Event event{action.is_cancel ? EventType::Cancel : EventType::AddLimit, action.order};
        for (const Trade& t : engine.process(event)) trades.push_back(t);
    }
    writer.flush();

    std::printf("recorded to %s\n", path.c_str());
    tools::print_summary(count, trades, engine);
    return 0;
}
