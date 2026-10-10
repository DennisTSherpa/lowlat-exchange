// Measures how long each single order book operation takes and prints
// percentiles. In trading, the slow outliers (p99, p99.9) matter more than
// the average, because the one slow order is the one that misses the trade.
//
// Usage: ./latency [number_of_actions]

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "exchange/order_book.hpp"
#include "workload.hpp"

using namespace exchange;
using Clock = std::chrono::steady_clock;

namespace {

long long percentile(const std::vector<long long>& sorted, double p) {
    std::size_t index = static_cast<std::size_t>(p / 100.0 * double(sorted.size() - 1));
    return sorted[index];
}

// How long reading the clock itself takes; it is included in every sample.
long long clock_overhead_ns() {
    std::vector<long long> samples(100'000);
    for (auto& s : samples) {
        auto a = Clock::now();
        auto b = Clock::now();
        s = std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
    }
    std::sort(samples.begin(), samples.end());
    return percentile(samples, 50);
}

}  // namespace

int main(int argc, char** argv) {
    std::size_t count = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1'000'000;
    const auto actions = bench::make_workload(count);

    OrderBook book;
    std::vector<long long> nanos;
    nanos.reserve(actions.size());
    std::size_t trades = 0;

    for (const auto& action : actions) {
        auto start = Clock::now();
        if (action.is_cancel) {
            book.cancel(action.order.id);
        } else {
            trades += book.add_limit(action.order).size();
        }
        auto end = Clock::now();
        nanos.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    }

    std::sort(nanos.begin(), nanos.end());
    std::printf("actions: %zu  trades: %zu  resting at end: %zu\n", actions.size(), trades,
                book.order_count());
    std::printf("clock overhead (included below): ~%lld ns\n\n", clock_overhead_ns());
    std::printf("| p50 | p90 | p99 | p99.9 | max |\n");
    std::printf("|-----|-----|-----|-------|-----|\n");
    std::printf("| %lld ns | %lld ns | %lld ns | %lld ns | %lld ns |\n", percentile(nanos, 50),
                percentile(nanos, 90), percentile(nanos, 99), percentile(nanos, 99.9),
                nanos.back());
    return 0;
}
