#include "exchange/tcp.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace exchange {

namespace {

[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error(what + ": " + std::strerror(errno));
}

// TCP is a byte stream: one read() can return part of a message, or the end
// of one message and the start of the next. These helpers loop until exactly
// `size` bytes have been read or written.
// read_exact returns false if the other side closed the connection cleanly
// before the message started.
bool read_exact(int fd, std::uint8_t* data, std::size_t size) {
    std::size_t done = 0;
    while (done < size) {
        ssize_t n = ::read(fd, data + done, size - done);
        if (n == 0) {
            if (done == 0) return false;
            throw std::runtime_error("connection closed in the middle of a message");
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            fail("read");
        }
        done += static_cast<std::size_t>(n);
    }
    return true;
}

void write_exact(int fd, const std::uint8_t* data, std::size_t size) {
    std::size_t done = 0;
    while (done < size) {
        ssize_t n = ::write(fd, data + done, size - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            fail("write");
        }
        done += static_cast<std::size_t>(n);
    }
}

void set_no_delay(int fd) {
    int on = 1;
    if (::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)) < 0) fail("setsockopt");
}

}  // namespace

wire::Response handle_request(MatchingEngine& engine, const Event& request) {
    const OrderId id = request.order.id;

    // Bytes from the network can hold anything. Reject unknown message
    // types or sides before they reach the engine.
    bool known_type = request.type == EventType::AddLimit || request.type == EventType::AddMarket ||
                      request.type == EventType::Cancel;
    bool known_side = request.order.side == Side::Buy || request.order.side == Side::Sell;
    if (!known_type || !known_side) {
        return wire::Response{wire::Status::Rejected, id, 0, 0};
    }

    bool valid = request.type == EventType::Cancel
                     ? engine.book().has_order(id)
                     : request.order.quantity > 0 && !engine.book().has_order(id);

    wire::Response response{valid ? wire::Status::Accepted : wire::Status::Rejected, id, 0, 0};
    // Every request goes to the engine (and its log), even rejected ones, so
    // a replay sees exactly what the server saw. Invalid requests change nothing.
    for (const Trade& trade : engine.process(request)) {
        response.filled_quantity += trade.quantity;
        response.trade_count += 1;
    }
    return response;
}

TcpServer::TcpServer(std::uint16_t port) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) fail("socket");

    int on = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        ::close(listen_fd_);
        fail("bind");
    }
    if (::listen(listen_fd_, 1) < 0) {
        ::close(listen_fd_);
        fail("listen");
    }

    // Find out which port we actually got (matters when port was 0).
    socklen_t length = sizeof(address);
    ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&address), &length);
    port_ = ntohs(address.sin_port);
}

TcpServer::~TcpServer() {
    if (listen_fd_ >= 0) ::close(listen_fd_);
}

std::size_t TcpServer::serve_one_client(MatchingEngine& engine) {
    int client = ::accept(listen_fd_, nullptr, nullptr);
    if (client < 0) fail("accept");
    set_no_delay(client);

    std::size_t handled = 0;
    try {
        wire::RequestBytes request{};
        while (read_exact(client, request.data(), request.size())) {
            wire::Response response = handle_request(engine, wire::decode_request(request));
            wire::ResponseBytes reply = wire::encode(response);
            write_exact(client, reply.data(), reply.size());
            ++handled;
        }
    } catch (...) {
        ::close(client);
        throw;
    }
    ::close(client);
    return handled;
}

TcpClient::TcpClient(const std::string& host, std::uint16_t port) {
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) fail("socket");

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        ::close(fd_);
        throw std::runtime_error("not an IPv4 address: " + host);
    }
    if (::connect(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        ::close(fd_);
        fail("connect");
    }
    set_no_delay(fd_);
}

TcpClient::~TcpClient() {
    if (fd_ >= 0) ::close(fd_);
}

wire::Response TcpClient::send(const Event& request) {
    wire::RequestBytes bytes = wire::encode(request);
    write_exact(fd_, bytes.data(), bytes.size());

    wire::ResponseBytes reply{};
    if (!read_exact(fd_, reply.data(), reply.size())) {
        throw std::runtime_error("server closed the connection");
    }
    return wire::decode_response(reply);
}

}  // namespace exchange
