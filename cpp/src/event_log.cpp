#include "exchange/event_log.hpp"

#include <stdexcept>

namespace exchange {

namespace {

template <typename T>
void write_field(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

template <typename T>
bool read_field(std::ifstream& in, T& value) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&value), sizeof(value)));
}

}  // namespace

EventLogWriter::EventLogWriter(const std::string& path)
    : out_(path, std::ios::binary | std::ios::trunc) {
    if (!out_) {
        throw std::runtime_error("cannot open event log for writing: " + path);
    }
}

void EventLogWriter::append(const Event& event) {
    write_field(out_, event.type);
    write_field(out_, event.order.side);
    write_field(out_, event.order.id);
    write_field(out_, event.order.price);
    write_field(out_, event.order.quantity);
}

void EventLogWriter::flush() { out_.flush(); }

std::vector<Event> read_event_log(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open event log for reading: " + path);
    }

    std::vector<Event> events;
    Event event{};
    while (read_field(in, event.type)) {
        bool complete = read_field(in, event.order.side) && read_field(in, event.order.id) &&
                        read_field(in, event.order.price) && read_field(in, event.order.quantity);
        if (!complete) {
            throw std::runtime_error("event log ends in the middle of a record: " + path);
        }
        events.push_back(event);
    }
    return events;
}

}  // namespace exchange
