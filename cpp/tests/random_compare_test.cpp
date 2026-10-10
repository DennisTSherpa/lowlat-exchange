// Feeds the same random stream of orders and cancels to the real OrderBook
// and to the slow ReferenceBook, and checks they agree after every step.
// If the fast book ever has a bug, this test finds an input that shows it.

#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "exchange/order_book.hpp"
#include "reference_book.hpp"

namespace exchange {
namespace {

void run_random_session(std::uint32_t seed, int steps) {
    std::mt19937 rng(seed);
    // A narrow price range makes orders cross often, which is where bugs hide.
    std::uniform_int_distribution<Price> price(95, 105);
    std::uniform_int_distribution<Quantity> quantity(1, 20);
    std::uniform_int_distribution<int> percent(0, 99);

    OrderBook book;
    testing::ReferenceBook reference;
    std::vector<OrderId> ids_used;
    OrderId next_id = 1;

    for (int step = 0; step < steps; ++step) {
        SCOPED_TRACE("seed " + std::to_string(seed) + ", step " + std::to_string(step));

        if (percent(rng) < 30 && !ids_used.empty()) {
            // Cancel some earlier id; it may already be filled or cancelled.
            std::uniform_int_distribution<std::size_t> pick(0, ids_used.size() - 1);
            OrderId id = ids_used[pick(rng)];
            ASSERT_EQ(book.cancel(id), reference.cancel(id));
        } else {
            Side side = percent(rng) < 50 ? Side::Buy : Side::Sell;
            Order order{next_id++, side, price(rng), quantity(rng)};
            ids_used.push_back(order.id);
            ASSERT_EQ(book.add_limit(order), reference.add_limit(order));
        }

        ASSERT_EQ(book.best_bid(), reference.best_bid());
        ASSERT_EQ(book.best_ask(), reference.best_ask());
        ASSERT_EQ(book.order_count(), reference.order_count());
        for (Price p = 95; p <= 105; ++p) {
            ASSERT_EQ(book.volume_at(Side::Buy, p), reference.volume_at(Side::Buy, p));
            ASSERT_EQ(book.volume_at(Side::Sell, p), reference.volume_at(Side::Sell, p));
        }
    }
}

TEST(RandomCompare, MatchesReferenceBook) {
    for (std::uint32_t seed = 1; seed <= 50; ++seed) {
        run_random_session(seed, 2000);
    }
}

}  // namespace
}  // namespace exchange
