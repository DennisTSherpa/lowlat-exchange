#pragma once

#include <random>
#include <vector>

#include "exchange/types.hpp"

namespace exchange::bench {

// One step of a benchmark: either add an order or cancel one.
struct Action {
    bool is_cancel;
    Order order;  // for a cancel, only order.id is used
};

// Builds a fixed, repeatable stream of actions (same seed, same stream),
// so benchmark runs on different machines measure the same work.
// Prices sit in a 100-tick band around a mid price, and about 30% of
// actions cancel a recent order, which keeps the book a realistic size.
inline std::vector<Action> make_workload(std::size_t count, std::uint32_t seed = 42) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<Price> offset(-50, 50);
    std::uniform_int_distribution<Quantity> quantity(1, 100);
    std::uniform_int_distribution<int> percent(0, 99);

    const Price mid = 10'000;
    std::vector<Action> actions;
    actions.reserve(count);
    OrderId next_id = 1;

    for (std::size_t i = 0; i < count; ++i) {
        if (percent(rng) < 30 && next_id > 1) {
            // Cancel one of the last 1000 orders sent.
            OrderId newest = next_id - 1;
            OrderId oldest = newest > 1000 ? newest - 1000 : 1;
            std::uniform_int_distribution<OrderId> pick(oldest, newest);
            actions.push_back({true, Order{pick(rng), Side::Buy, 0, 0}});
        } else {
            Side side = percent(rng) < 50 ? Side::Buy : Side::Sell;
            actions.push_back({false, Order{next_id++, side, mid + offset(rng), quantity(rng)}});
        }
    }
    return actions;
}

}  // namespace exchange::bench
