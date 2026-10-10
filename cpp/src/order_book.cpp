#include "exchange/order_book.hpp"

#include <algorithm>
#include <iterator>

namespace exchange {

std::vector<Trade> OrderBook::add_limit(const Order& order) {
    std::vector<Trade> trades;
    if (order.quantity == 0 || locations_.contains(order.id)) {
        return trades;
    }

    Order incoming = order;
    if (incoming.side == Side::Buy) {
        // A buy crosses any ask priced at or below its limit.
        match(incoming, asks_, [&](Price ask) { return ask <= incoming.price; }, trades);
        rest(incoming, bids_);
    } else {
        // A sell crosses any bid priced at or above its limit.
        match(incoming, bids_, [&](Price bid) { return bid >= incoming.price; }, trades);
        rest(incoming, asks_);
    }
    return trades;
}

template <typename OppositeSide, typename Crosses>
void OrderBook::match(Order& incoming, OppositeSide& opposite, Crosses crosses,
                      std::vector<Trade>& trades) {
    while (incoming.quantity > 0 && !opposite.empty()) {
        auto best = opposite.begin();
        if (!crosses(best->first)) {
            break;  // the best opposite price is too far away; nothing more trades
        }

        Level& level = best->second;
        Order& resting = level.front();  // oldest order at this price
        Quantity filled = std::min(incoming.quantity, resting.quantity);

        trades.push_back(Trade{resting.id, incoming.id, resting.price, filled});
        incoming.quantity -= filled;
        resting.quantity -= filled;

        if (resting.quantity == 0) {
            locations_.erase(resting.id);
            level.pop_front();
            if (level.empty()) {
                opposite.erase(best);
            }
        }
    }
}

template <typename SameSide>
void OrderBook::rest(const Order& order, SameSide& same) {
    if (order.quantity == 0) {
        return;  // fully filled, nothing left to rest
    }
    Level& level = same[order.price];
    level.push_back(order);
    locations_[order.id] = Location{order.side, order.price, std::prev(level.end())};
}

bool OrderBook::cancel(OrderId id) {
    auto found = locations_.find(id);
    if (found == locations_.end()) {
        return false;
    }
    const Location& where = found->second;

    auto remove_from = [&](auto& side) {
        auto level = side.find(where.price);
        level->second.erase(where.position);
        if (level->second.empty()) {
            side.erase(level);
        }
    };
    if (where.side == Side::Buy) {
        remove_from(bids_);
    } else {
        remove_from(asks_);
    }

    locations_.erase(found);
    return true;
}

std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

Quantity OrderBook::volume_at(Side side, Price price) const {
    auto sum = [&](const auto& book_side) -> Quantity {
        auto level = book_side.find(price);
        if (level == book_side.end()) return 0;
        Quantity total = 0;
        for (const Order& order : level->second) total += order.quantity;
        return total;
    };
    return side == Side::Buy ? sum(bids_) : sum(asks_);
}

}  // namespace exchange
