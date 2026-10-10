#pragma once

#include <array>
#include <cstdint>
#include <cstring>

#include "exchange/event_log.hpp"

namespace exchange::wire {

// Fixed-size binary messages. The same 22-byte request record is used on
// the network and in the event log file, so a log is literally a recording
// of the requests the engine received.
//
// Request (client -> server), 22 bytes:
//   type (1) | side (1) | order id (8) | price (8) | quantity (4)
//
// Response (server -> client), 17 bytes, exactly one per request:
//   status (1) | order id (8) | filled quantity (4) | number of trades (4)
//
// Numbers use the machine's native byte order (little-endian on x86 and
// ARM Linux), so client and server must run on the same kind of machine.
// Fields are copied with memcpy, which avoids any struct padding questions.

constexpr std::size_t kRequestSize = 22;
constexpr std::size_t kResponseSize = 17;

using RequestBytes = std::array<std::uint8_t, kRequestSize>;
using ResponseBytes = std::array<std::uint8_t, kResponseSize>;

enum class Status : std::uint8_t { Accepted = 'A', Rejected = 'R' };

struct Response {
    Status status;
    OrderId order_id;
    Quantity filled_quantity;
    std::uint32_t trade_count;

    bool operator==(const Response&) const = default;
};

namespace detail {
template <typename T>
void put(std::uint8_t*& out, const T& value) {
    std::memcpy(out, &value, sizeof(value));
    out += sizeof(value);
}
template <typename T>
void get(const std::uint8_t*& in, T& value) {
    std::memcpy(&value, in, sizeof(value));
    in += sizeof(value);
}
}  // namespace detail

inline RequestBytes encode(const Event& event) {
    RequestBytes bytes{};
    std::uint8_t* out = bytes.data();
    detail::put(out, event.type);
    detail::put(out, event.order.side);
    detail::put(out, event.order.id);
    detail::put(out, event.order.price);
    detail::put(out, event.order.quantity);
    return bytes;
}

inline Event decode_request(const RequestBytes& bytes) {
    Event event{};
    const std::uint8_t* in = bytes.data();
    detail::get(in, event.type);
    detail::get(in, event.order.side);
    detail::get(in, event.order.id);
    detail::get(in, event.order.price);
    detail::get(in, event.order.quantity);
    return event;
}

inline ResponseBytes encode(const Response& response) {
    ResponseBytes bytes{};
    std::uint8_t* out = bytes.data();
    detail::put(out, response.status);
    detail::put(out, response.order_id);
    detail::put(out, response.filled_quantity);
    detail::put(out, response.trade_count);
    return bytes;
}

inline Response decode_response(const ResponseBytes& bytes) {
    Response response{};
    const std::uint8_t* in = bytes.data();
    detail::get(in, response.status);
    detail::get(in, response.order_id);
    detail::get(in, response.filled_quantity);
    detail::get(in, response.trade_count);
    return response;
}

}  // namespace exchange::wire
