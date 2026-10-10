#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include "exchange/types.hpp"

namespace exchange::testing {

// A deliberately slow order book that is easy to check by eye.
// Every resting order sits in one flat vector with an arrival number.
// To match, it scans the whole vector for the best price, breaking ties
// by the earliest arrival. The randomized test checks that the real
// OrderBook always gives exactly the same answers as this one.
class ReferenceBook {
public:
    std::vector<Trade> add_limit(const Order& order) { return add(order, false); }
    std::vector<Trade> add_market(const Order& order) { return add(order, true); }

    bool cancel(OrderId id) {
        auto it = find(id);
        if (it == resting_.end()) return false;
        resting_.erase(it);
        return true;
    }

    std::optional<Price> best_bid() const { return best_price(Side::Buy); }
    std::optional<Price> best_ask() const { return best_price(Side::Sell); }

    Quantity volume_at(Side side, Price price) const {
        Quantity total = 0;
        for (const Entry& e : resting_) {
            if (e.order.side == side && e.order.price == price) total += e.order.quantity;
        }
        return total;
    }

    std::size_t order_count() const { return resting_.size(); }

private:
    std::vector<Trade> add(const Order& order, bool is_market) {
        std::vector<Trade> trades;
        if (order.quantity == 0 || find(order.id) != resting_.end()) {
            return trades;
        }

        Order incoming = order;
        while (incoming.quantity > 0) {
            auto best = best_opposite(incoming, is_market);
            if (best == resting_.end()) break;

            Quantity filled = std::min(incoming.quantity, best->order.quantity);
            trades.push_back(Trade{best->order.id, incoming.id, best->order.price, filled});
            incoming.quantity -= filled;
            best->order.quantity -= filled;
            if (best->order.quantity == 0) resting_.erase(best);
        }

        if (incoming.quantity > 0 && !is_market) {
            resting_.push_back(Entry{incoming, next_arrival_++});
        }
        return trades;
    }

    struct Entry {
        Order order;
        std::uint64_t arrival;
    };

    std::vector<Entry>::iterator find(OrderId id) {
        return std::find_if(resting_.begin(), resting_.end(),
                            [&](const Entry& e) { return e.order.id == id; });
    }

    // The resting order the incoming one should trade with next, or end().
    std::vector<Entry>::iterator best_opposite(const Order& incoming, bool is_market) {
        auto best = resting_.end();
        for (auto it = resting_.begin(); it != resting_.end(); ++it) {
            const Order& r = it->order;
            if (r.side == incoming.side) continue;
            bool crosses = is_market || (incoming.side == Side::Buy ? r.price <= incoming.price
                                                                    : r.price >= incoming.price);
            if (!crosses) continue;
            if (best == resting_.end() || better(*it, *best)) best = it;
        }
        return best;
    }

    // True if resting entry a should trade before resting entry b.
    static bool better(const Entry& a, const Entry& b) {
        if (a.order.price != b.order.price) {
            return a.order.side == Side::Buy ? a.order.price > b.order.price
                                             : a.order.price < b.order.price;
        }
        return a.arrival < b.arrival;
    }

    std::optional<Price> best_price(Side side) const {
        std::optional<Price> best;
        for (const Entry& e : resting_) {
            if (e.order.side != side) continue;
            if (!best || (side == Side::Buy ? e.order.price > *best : e.order.price < *best)) {
                best = e.order.price;
            }
        }
        return best;
    }

    std::vector<Entry> resting_;
    std::uint64_t next_arrival_ = 0;
};

}  // namespace exchange::testing
