#include "exchange/event_log.hpp"

#include <stdexcept>

#include "exchange/wire.hpp"

namespace exchange {

// Each log record is the same 22-byte request used on the network
// (see wire.hpp), written one after another.

EventLogWriter::EventLogWriter(const std::string& path)
    : out_(path, std::ios::binary | std::ios::trunc) {
    if (!out_) {
        throw std::runtime_error("cannot open event log for writing: " + path);
    }
}

void EventLogWriter::append(const Event& event) {
    wire::RequestBytes bytes = wire::encode(event);
    out_.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void EventLogWriter::flush() { out_.flush(); }

std::vector<Event> read_event_log(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open event log for reading: " + path);
    }

    std::vector<Event> events;
    wire::RequestBytes bytes{};
    while (in.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) {
        events.push_back(wire::decode_request(bytes));
    }
    if (in.gcount() != 0) {
        throw std::runtime_error("event log ends in the middle of a record: " + path);
    }
    return events;
}

}  // namespace exchange
