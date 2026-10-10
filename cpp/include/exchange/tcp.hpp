#pragma once

#include <cstdint>
#include <string>

#include "exchange/matching_engine.hpp"
#include "exchange/wire.hpp"

namespace exchange {

// A deliberately simple order-entry server built on plain blocking POSIX
// sockets:
//   - one client at a time,
//   - for each 22-byte request: apply it to the engine, send one 17-byte
//     response, then read the next request.
// TCP_NODELAY is set so small responses go out immediately instead of
// being held back to be combined with later data (Nagle's algorithm).
class TcpServer {
public:
    // Listens on 127.0.0.1:port. Pass port 0 to let the OS pick a free port.
    explicit TcpServer(std::uint16_t port);
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    std::uint16_t port() const { return port_; }

    // Waits for one client and serves its requests until it disconnects.
    // Returns the number of requests handled.
    std::size_t serve_one_client(MatchingEngine& engine);

private:
    int listen_fd_ = -1;
    std::uint16_t port_ = 0;
};

// Applies one request to the engine and builds the response. Kept separate
// from the socket code so it can be tested without a network.
wire::Response handle_request(MatchingEngine& engine, const Event& request);

class TcpClient {
public:
    TcpClient(const std::string& host, std::uint16_t port);
    ~TcpClient();

    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    // Sends one request and waits for its response.
    wire::Response send(const Event& request);

private:
    int fd_ = -1;
};

}  // namespace exchange
