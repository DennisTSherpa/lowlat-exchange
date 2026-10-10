#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>

#include "exchange/matching_engine.hpp"

namespace exchange::tools {

// A fingerprint of every trade (FNV-1a hash). If two runs print the same
// fingerprint, they produced the same trades in the same order.
inline std::uint64_t fingerprint(const std::vector<Trade>& trades) {
    std::uint64_t hash = 1469598103934665603ULL;
    auto mix = [&](std::uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= (value >> (byte * 8)) & 0xff;
            hash *= 1099511628211ULL;
        }
    };
    for (const Trade& t : trades) {
        mix(t.maker_id);
        mix(t.taker_id);
        mix(static_cast<std::uint64_t>(t.price));
        mix(t.quantity);
    }
    return hash;
}

inline void print_summary(std::size_t events, const std::vector<Trade>& trades,
                          const MatchingEngine& engine) {
    const OrderBook& book = engine.book();
    std::printf("events:            %zu\n", events);
    std::printf("trades:            %zu\n", trades.size());
    std::printf("resting orders:    %zu\n", book.order_count());
    std::printf("best bid / ask:    %lld / %lld\n",
                static_cast<long long>(book.best_bid().value_or(0)),
                static_cast<long long>(book.best_ask().value_or(0)));
    std::printf("trade fingerprint: %016llx\n",
                static_cast<unsigned long long>(fingerprint(trades)));
}

}  // namespace exchange::tools
