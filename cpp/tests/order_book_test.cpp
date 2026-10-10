#include "exchange/order_book.hpp"

#include <gtest/gtest.h>

namespace exchange {
namespace {

Order buy(OrderId id, Price price, Quantity qty) { return {id, Side::Buy, price, qty}; }
Order sell(OrderId id, Price price, Quantity qty) { return {id, Side::Sell, price, qty}; }

TEST(OrderBook, EmptyBookHasNoPrices) {
    OrderBook book;
    EXPECT_FALSE(book.best_bid());
    EXPECT_FALSE(book.best_ask());
    EXPECT_EQ(book.order_count(), 0u);
}

TEST(OrderBook, NonCrossingOrdersRest) {
    OrderBook book;
    EXPECT_TRUE(book.add_limit(buy(1, 99, 10)).empty());
    EXPECT_TRUE(book.add_limit(sell(2, 101, 5)).empty());
    EXPECT_EQ(book.best_bid(), 99);
    EXPECT_EQ(book.best_ask(), 101);
    EXPECT_EQ(book.volume_at(Side::Buy, 99), 10u);
    EXPECT_EQ(book.volume_at(Side::Sell, 101), 5u);
}

TEST(OrderBook, BestPriceIsHighestBidAndLowestAsk) {
    OrderBook book;
    book.add_limit(buy(1, 98, 1));
    book.add_limit(buy(2, 99, 1));
    book.add_limit(sell(3, 102, 1));
    book.add_limit(sell(4, 101, 1));
    EXPECT_EQ(book.best_bid(), 99);
    EXPECT_EQ(book.best_ask(), 101);
}

TEST(OrderBook, CrossingOrderTradesAtRestingPrice) {
    OrderBook book;
    book.add_limit(sell(1, 100, 10));
    auto trades = book.add_limit(buy(2, 105, 4));
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0], (Trade{1, 2, 100, 4}));  // maker's price, not 105
    EXPECT_EQ(book.volume_at(Side::Sell, 100), 6u);
    EXPECT_FALSE(book.best_bid());  // buy fully filled, nothing rests
}

TEST(OrderBook, SamePriceFillsOldestFirst) {
    OrderBook book;
    book.add_limit(sell(1, 100, 5));
    book.add_limit(sell(2, 100, 5));
    auto trades = book.add_limit(buy(3, 100, 7));
    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0], (Trade{1, 3, 100, 5}));
    EXPECT_EQ(trades[1], (Trade{2, 3, 100, 2}));
    EXPECT_EQ(book.volume_at(Side::Sell, 100), 3u);
}

TEST(OrderBook, SweepsSeveralLevelsThenRestsTheRest) {
    OrderBook book;
    book.add_limit(sell(1, 100, 2));
    book.add_limit(sell(2, 101, 2));
    book.add_limit(sell(3, 103, 2));
    auto trades = book.add_limit(buy(4, 102, 10));
    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0], (Trade{1, 4, 100, 2}));
    EXPECT_EQ(trades[1], (Trade{2, 4, 101, 2}));
    EXPECT_EQ(book.best_bid(), 102);  // 6 left over rests as a bid
    EXPECT_EQ(book.volume_at(Side::Buy, 102), 6u);
    EXPECT_EQ(book.best_ask(), 103);
}

TEST(OrderBook, CancelRemovesOrderAndEmptyLevel) {
    OrderBook book;
    book.add_limit(buy(1, 99, 10));
    book.add_limit(buy(2, 98, 10));
    EXPECT_TRUE(book.cancel(1));
    EXPECT_EQ(book.best_bid(), 98);
    EXPECT_FALSE(book.cancel(1));  // already gone
    EXPECT_FALSE(book.cancel(42));  // never existed
    EXPECT_EQ(book.order_count(), 1u);
}

TEST(OrderBook, CancelledOrderLosesItsPlaceInLine) {
    OrderBook book;
    book.add_limit(sell(1, 100, 5));
    book.add_limit(sell(2, 100, 5));
    book.cancel(1);
    auto trades = book.add_limit(buy(3, 100, 5));
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].maker_id, 2u);
}

TEST(OrderBook, IgnoresZeroQuantityAndDuplicateIds) {
    OrderBook book;
    book.add_limit(buy(1, 99, 0));
    EXPECT_EQ(book.order_count(), 0u);
    book.add_limit(buy(2, 99, 5));
    book.add_limit(buy(2, 98, 5));
    EXPECT_EQ(book.order_count(), 1u);
    EXPECT_EQ(book.volume_at(Side::Buy, 98), 0u);
}

}  // namespace
}  // namespace exchange
