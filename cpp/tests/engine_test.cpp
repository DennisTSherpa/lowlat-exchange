#include "exchange/matching_engine.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <random>

namespace exchange {
namespace {

std::string temp_path(const std::string& name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

std::vector<Event> random_events(std::uint32_t seed, int count) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<Price> price(95, 105);
    std::uniform_int_distribution<Quantity> quantity(1, 20);
    std::uniform_int_distribution<int> percent(0, 99);

    std::vector<Event> events;
    OrderId next_id = 1;
    for (int i = 0; i < count; ++i) {
        int roll = percent(rng);
        Side side = percent(rng) < 50 ? Side::Buy : Side::Sell;
        if (roll < 30 && next_id > 1) {
            std::uniform_int_distribution<OrderId> pick(1, next_id - 1);
            events.push_back({EventType::Cancel, Order{pick(rng), side, 0, 0}});
        } else if (roll < 40) {
            events.push_back({EventType::AddMarket, Order{next_id++, side, 0, quantity(rng)}});
        } else {
            events.push_back(
                {EventType::AddLimit, Order{next_id++, side, price(rng), quantity(rng)}});
        }
    }
    return events;
}

TEST(EventLog, WritesAndReadsBackTheSameEvents) {
    std::string path = temp_path("exchange_log_roundtrip.bin");
    auto events = random_events(7, 500);
    {
        EventLogWriter writer(path);
        for (const Event& e : events) writer.append(e);
    }
    EXPECT_EQ(read_event_log(path), events);
    EXPECT_EQ(std::filesystem::file_size(path), events.size() * 22);
    std::remove(path.c_str());
}

TEST(EventLog, RejectsTruncatedFile) {
    std::string path = temp_path("exchange_log_truncated.bin");
    {
        EventLogWriter writer(path);
        writer.append({EventType::AddLimit, Order{1, Side::Buy, 100, 5}});
    }
    std::filesystem::resize_file(path, 10);  // cut the record in half
    EXPECT_THROW(read_event_log(path), std::runtime_error);
    std::remove(path.c_str());
}

// The key property: a recorded session, replayed into a fresh engine,
// produces exactly the same trades and ends in exactly the same book.
TEST(MatchingEngine, ReplayReproducesTheSessionExactly) {
    std::string path = temp_path("exchange_log_replay.bin");
    auto events = random_events(11, 20'000);

    std::vector<Trade> live_trades;
    EventLogWriter writer(path);
    MatchingEngine live(&writer);
    for (const Event& e : events) {
        for (const Trade& t : live.process(e)) live_trades.push_back(t);
    }
    writer.flush();
    ASSERT_FALSE(live_trades.empty());

    MatchingEngine replayed;
    std::vector<Trade> replay_trades = replay(path, replayed);

    EXPECT_EQ(replay_trades, live_trades);
    EXPECT_EQ(replayed.book().best_bid(), live.book().best_bid());
    EXPECT_EQ(replayed.book().best_ask(), live.book().best_ask());
    EXPECT_EQ(replayed.book().order_count(), live.book().order_count());
    for (Price p = 95; p <= 105; ++p) {
        EXPECT_EQ(replayed.book().volume_at(Side::Buy, p), live.book().volume_at(Side::Buy, p));
        EXPECT_EQ(replayed.book().volume_at(Side::Sell, p), live.book().volume_at(Side::Sell, p));
    }
    std::remove(path.c_str());
}

}  // namespace
}  // namespace exchange
