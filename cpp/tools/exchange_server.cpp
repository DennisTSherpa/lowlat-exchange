// Runs the matching engine behind a TCP socket on 127.0.0.1.
//
//   ./exchange_server [port] [log_file]
//
// Serves one client at a time, forever. If a log file is given, every
// request is recorded so the session can be replayed with replay_session.

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>

#include "exchange/tcp.hpp"

using namespace exchange;

int main(int argc, char** argv) {
    auto port = static_cast<std::uint16_t>(argc > 1 ? std::atoi(argv[1]) : 9000);

    try {
        std::unique_ptr<EventLogWriter> log;
        if (argc > 2) log = std::make_unique<EventLogWriter>(argv[2]);

        MatchingEngine engine(log.get());
        TcpServer server(port);
        std::printf("listening on 127.0.0.1:%u\n", server.port());
        std::fflush(stdout);

        while (true) {
            std::size_t handled = server.serve_one_client(engine);
            if (log) log->flush();
            std::printf("client disconnected after %zu requests; resting orders: %zu\n", handled,
                        engine.book().order_count());
            std::fflush(stdout);
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
}
