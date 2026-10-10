// Sends the benchmark workload to a running exchange_server, one request
// at a time, and measures the round trip of each: from just before the
// request is written to the socket until its response has been read.
//
//   ./load_client [port] [number_of_requests]
//
// This is end-to-end latency: client socket write, kernel loopback, server
// read, matching, server write, client read.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <vector>

#include "../bench/workload.hpp"
#include "exchange/tcp.hpp"

using namespace exchange;
using Clock = std::chrono::steady_clock;

namespace {

long long percentile(const std::vector<long long>& sorted, double p) {
    return sorted[static_cast<std::size_t>(p / 100.0 * double(sorted.size() - 1))];
}

}  // namespace

int main(int argc, char** argv) {
    auto port = static_cast<std::uint16_t>(argc > 1 ? std::atoi(argv[1]) : 9000);
    std::size_t count = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 100'000;

    try {
        TcpClient client("127.0.0.1", port);
        std::vector<long long> nanos;
        nanos.reserve(count);
        std::size_t accepted = 0;
        std::size_t trades = 0;

        for (const auto& action : bench::make_workload(count)) {
            Event request{action.is_cancel ? EventType::Cancel : EventType::AddLimit, action.order};

            auto start = Clock::now();
            wire::Response response = client.send(request);
            auto end = Clock::now();

            nanos.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
            accepted += response.status == wire::Status::Accepted;
            trades += response.trade_count;
        }

        std::sort(nanos.begin(), nanos.end());
        std::printf("requests: %zu  accepted: %zu  trades: %zu\n\n", nanos.size(), accepted, trades);
        std::printf("| p50 | p90 | p99 | p99.9 | max |\n");
        std::printf("|-----|-----|-----|-------|-----|\n");
        std::printf("| %.1f us | %.1f us | %.1f us | %.1f us | %.1f us |\n",
                    percentile(nanos, 50) / 1000.0, percentile(nanos, 90) / 1000.0,
                    percentile(nanos, 99) / 1000.0, percentile(nanos, 99.9) / 1000.0,
                    nanos.back() / 1000.0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
    return 0;
}
