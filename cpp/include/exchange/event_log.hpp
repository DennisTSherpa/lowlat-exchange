#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "exchange/types.hpp"

namespace exchange {

// One input to the exchange. The engine's output (trades, book state)
// depends only on the list of events and their order, so saving the
// events is enough to rebuild everything later.
enum class EventType : std::uint8_t { AddLimit, AddMarket, Cancel };

struct Event {
    EventType type;
    Order order;  // for Cancel, only order.id is used

    bool operator==(const Event&) const = default;
};

// Appends events to a binary file, one fixed-size 22-byte record per event,
// in exactly the format used for requests on the network (see wire.hpp).
// Fixed-size records mean there is nothing to parse: the reader copies
// 22 bytes at a time back into the same fields.
class EventLogWriter {
public:
    explicit EventLogWriter(const std::string& path);
    void append(const Event& event);
    void flush();

private:
    std::ofstream out_;
};

// Reads back every event in a file written by EventLogWriter.
std::vector<Event> read_event_log(const std::string& path);

}  // namespace exchange
