// Rebuilds a session from its event log and prints the same summary that
// record_session printed. Matching fingerprints prove the engine is
// deterministic.
//
//   ./replay_session session.bin

#include <cstdio>
#include <exception>

#include "exchange/matching_engine.hpp"
#include "session_summary.hpp"

using namespace exchange;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <log_file>\n", argv[0]);
        return 1;
    }

    try {
        MatchingEngine engine;
        auto events = read_event_log(argv[1]);
        std::vector<Trade> trades;
        for (const Event& event : events) {
            for (const Trade& t : engine.process(event)) trades.push_back(t);
        }
        std::printf("replayed %s\n", argv[1]);
        tools::print_summary(events.size(), trades, engine);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
    return 0;
}
