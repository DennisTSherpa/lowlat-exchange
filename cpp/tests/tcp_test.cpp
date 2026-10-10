#include "exchange/tcp.hpp"

#include <gtest/gtest.h>

#include <thread>

namespace exchange {
namespace {

Event limit(OrderId id, Side side, Price price, Quantity qty) {
    return {EventType::AddLimit, Order{id, side, price, qty}};
}
Event cancel(OrderId id) { return {EventType::Cancel, Order{id, Side::Buy, 0, 0}}; }

TEST(Wire, RequestRoundTrip) {
    Event event = limit(123456789, Side::Sell, -42, 7);
    EXPECT_EQ(wire::decode_request(wire::encode(event)), event);
}

TEST(Wire, ResponseRoundTrip) {
    wire::Response response{wire::Status::Rejected, 99, 1000, 3};
    EXPECT_EQ(wire::decode_response(wire::encode(response)), response);
}

TEST(HandleRequest, ReportsFillsAndRejections) {
    MatchingEngine engine;
    auto r1 = handle_request(engine, limit(1, Side::Sell, 100, 5));
    EXPECT_EQ(r1, (wire::Response{wire::Status::Accepted, 1, 0, 0}));

    auto r2 = handle_request(engine, limit(2, Side::Buy, 100, 8));
    EXPECT_EQ(r2, (wire::Response{wire::Status::Accepted, 2, 5, 1}));

    // Order 2 has 3 left resting; a second order with the same id is rejected.
    auto r3 = handle_request(engine, limit(2, Side::Buy, 99, 1));
    EXPECT_EQ(r3.status, wire::Status::Rejected);

    EXPECT_EQ(handle_request(engine, cancel(2)).status, wire::Status::Accepted);
    EXPECT_EQ(handle_request(engine, cancel(2)).status, wire::Status::Rejected);
    EXPECT_EQ(handle_request(engine, limit(3, Side::Buy, 99, 0)).status, wire::Status::Rejected);
}

TEST(HandleRequest, RejectsGarbageWithoutTouchingTheBook) {
    MatchingEngine engine;
    Event bad = limit(1, Side::Buy, 100, 5);
    bad.type = static_cast<EventType>(77);
    EXPECT_EQ(handle_request(engine, bad).status, wire::Status::Rejected);

    bad = limit(2, Side::Buy, 100, 5);
    bad.order.side = static_cast<Side>(9);
    EXPECT_EQ(handle_request(engine, bad).status, wire::Status::Rejected);
    EXPECT_EQ(engine.book().order_count(), 0u);
}

// A real server on a real socket (localhost, port chosen by the OS).
TEST(Tcp, ClientAndServerTalkOverLoopback) {
    MatchingEngine engine;
    TcpServer server(0);
    std::thread server_thread([&] { server.serve_one_client(engine); });

    {
        TcpClient client("127.0.0.1", server.port());
        EXPECT_EQ(client.send(limit(1, Side::Sell, 100, 5)).status, wire::Status::Accepted);
        EXPECT_EQ(client.send(limit(2, Side::Sell, 101, 5)).status, wire::Status::Accepted);
        auto fill = client.send(limit(3, Side::Buy, 101, 7));
        EXPECT_EQ(fill, (wire::Response{wire::Status::Accepted, 3, 7, 2}));
        EXPECT_EQ(client.send(cancel(42)).status, wire::Status::Rejected);
    }  // client disconnects here, so serve_one_client returns

    server_thread.join();
    EXPECT_EQ(engine.book().best_ask(), 101);
    EXPECT_EQ(engine.book().volume_at(Side::Sell, 101), 3u);
}

}  // namespace
}  // namespace exchange
