#pragma once

#include <string>
#include <vector>

#include "exchange/event_log.hpp"
#include "exchange/order_book.hpp"

namespace exchange {

// The single entry point for everything that changes the book.
// Each event is written to the log (if there is one) before it is applied,
// so the log is always a complete record of what the engine was asked to do.
//
// The engine is deterministic: it has no clocks, randomness or threads, so
// replaying the same log always produces exactly the same trades. That is
// what lets you reproduce any bug from a recorded session.
class MatchingEngine {
public:
    explicit MatchingEngine(EventLogWriter* log = nullptr) : log_(log) {}

    std::vector<Trade> process(const Event& event);

    const OrderBook& book() const { return book_; }

private:
    OrderBook book_;
    EventLogWriter* log_;
};

// Feeds every event in a log file into the engine, in order, and returns
// all the trades that came out.
std::vector<Trade> replay(const std::string& log_path, MatchingEngine& engine);

}  // namespace exchange
