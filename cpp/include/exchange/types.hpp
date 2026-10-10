#pragma once

#include <cstdint>

namespace exchange {

// Prices are whole numbers of "ticks" (the smallest price step), never
// floating point. With a tick of $0.01, a price of 10050 means $100.50.
// Integers compare exactly, so two orders at "the same price" really match.
using Price = std::int64_t;
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;

enum class Side : std::uint8_t { Buy, Sell };

struct Order {
    OrderId id;
    Side side;
    Price price;
    Quantity quantity;

    bool operator==(const Order&) const = default;
};

// A trade always happens at the price of the order that was already
// resting in the book (the "maker"), not the incoming order (the "taker").
struct Trade {
    OrderId maker_id;
    OrderId taker_id;
    Price price;
    Quantity quantity;

    bool operator==(const Trade&) const = default;
};

}  // namespace exchange
