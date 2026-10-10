#pragma once

#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "exchange/types.hpp"

namespace exchange {

// A limit order book with price-time priority:
//   1. Better prices trade first (highest bid, lowest ask).
//   2. At the same price, the order that arrived first trades first.
//
// This is the simple version, built only from standard containers:
//   - each side is a std::map from price to a queue of orders (a "level"),
//   - each level is a std::list, so the oldest order is at the front,
//   - an unordered_map from order id to its place in the book makes
//     cancel fast without searching.
class OrderBook {
public:
    // Matches the order against the opposite side, then rests whatever
    // quantity is left in the book. Returns the trades it produced, in order.
    // Orders with zero quantity or an id already in the book are ignored.
    std::vector<Trade> add_limit(const Order& order);

    // Removes a resting order. Returns false if the id is not in the book
    // (never added, already cancelled, or already fully filled).
    bool cancel(OrderId id);

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;

    // Total resting quantity at one price on one side (0 if none).
    Quantity volume_at(Side side, Price price) const;

    // Number of orders currently resting in the book.
    std::size_t order_count() const { return locations_.size(); }

private:
    using Level = std::list<Order>;
    // Bids are sorted highest first and asks lowest first, so the best
    // price on either side is always begin().
    using Bids = std::map<Price, Level, std::greater<Price>>;
    using Asks = std::map<Price, Level, std::less<Price>>;

    struct Location {
        Side side;
        Price price;
        Level::iterator position;
    };

    template <typename OppositeSide, typename Crosses>
    void match(Order& incoming, OppositeSide& opposite, Crosses crosses,
               std::vector<Trade>& trades);

    template <typename SameSide>
    void rest(const Order& order, SameSide& same);

    Bids bids_;
    Asks asks_;
    std::unordered_map<OrderId, Location> locations_;
};

}  // namespace exchange
