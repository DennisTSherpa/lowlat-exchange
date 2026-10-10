// Google Benchmark microbenchmarks: average throughput of the order book.
// For the latency distribution (p50 / p99 / p99.9), see latency.cpp.

#include <benchmark/benchmark.h>

#include "exchange/order_book.hpp"
#include "workload.hpp"

namespace exchange::bench {
namespace {

// Adds orders that never cross, so this measures resting an order only.
void BM_AddNonCrossing(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        OrderBook book;
        state.ResumeTiming();
        for (OrderId id = 1; id <= 1000; ++id) {
            Side side = id % 2 ? Side::Buy : Side::Sell;
            Price price = side == Side::Buy ? 9'900 + Price(id % 50) : 10'100 + Price(id % 50);
            benchmark::DoNotOptimize(book.add_limit({id, side, price, 10}));
        }
    }
    state.SetItemsProcessed(state.iterations() * 1000);
}
BENCHMARK(BM_AddNonCrossing);

// A realistic mix of adds that rest, adds that trade, and cancels.
void BM_MixedWorkload(benchmark::State& state) {
    const auto actions = make_workload(100'000);
    for (auto _ : state) {
        state.PauseTiming();
        OrderBook book;
        state.ResumeTiming();
        for (const Action& action : actions) {
            if (action.is_cancel) {
                benchmark::DoNotOptimize(book.cancel(action.order.id));
            } else {
                benchmark::DoNotOptimize(book.add_limit(action.order));
            }
        }
    }
    state.SetItemsProcessed(state.iterations() * std::int64_t(actions.size()));
}
BENCHMARK(BM_MixedWorkload)->Unit(benchmark::kMillisecond);

}  // namespace
}  // namespace exchange::bench

BENCHMARK_MAIN();
