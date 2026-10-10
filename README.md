# Low-Latency Exchange

A stock-exchange-style matching engine in C++, built step by step, with every latency claim backed by a benchmark you can rerun.

The rule for this project is **simplest version first**. Each part starts with plain standard-library code that someone with foundation knowledge of C++ can follow. An optimization only goes in when a benchmark shows it helps, and the README explains it in plain words.

## Status

| Milestone | What | State |
|-----------|------|-------|
| 1 | Order book: add, cancel, match, with tests and benchmarks | ✅ done |
| 2 | Matching engine: market orders, event log, replay | next |
| 3 | Order entry over TCP, end-to-end latency | planned |
| 4 | Python: bindings, market simulator, backtest | planned |
| 5 | Web view of the live book | planned |

## Build and test

You need a Linux machine with CMake 3.20+, Ninja, and GCC 12+ or Clang 15+.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/latency
```

GoogleTest and Google Benchmark are downloaded automatically the first time you run CMake.

## How the order book works

An **order book** lists everyone who wants to buy (bids) and sell (asks) at each price. A new order that can trade with the other side does so straight away. Whatever quantity is left rests in the book and waits.

The book uses **price-time priority**:
1. Better prices go first: the highest bid and the lowest ask.
2. At the same price, the earliest order goes first.

Every trade happens at the price of the order that was already waiting (the *maker*), not the price of the newcomer (the *taker*).

### Data structures ([`order_book.hpp`](cpp/include/exchange/order_book.hpp))

```
bids:  std::map<price, list of orders>   sorted high → low, best bid = begin()
asks:  std::map<price, list of orders>   sorted low → high, best ask = begin()
index: std::unordered_map<order id, where the order sits>   for fast cancel
```

- **Matching a buy:** while the lowest ask is at or below the buy's price, trade with the oldest order at that price, then remove anything that's fully filled.
- **Resting:** append to the back of the list at that price, which keeps arrival order.
- **Cancel:** look the id up in the index and erase it from its list directly. There's no search.
- **Prices are integers** (ticks), not `double`, so prices compare exactly.

## How we know it's correct

- **Unit tests** ([`order_book_test.cpp`](cpp/tests/order_book_test.cpp)) cover each rule on its own: priority, partial fills, sweeping several levels, and cancels.
- **Randomized comparison** ([`random_compare_test.cpp`](cpp/tests/random_compare_test.cpp)): a slow [reference book](cpp/tests/reference_book.hpp) that just scans a flat list is easy to check by eye. The test sends 100,000 random orders and cancels to both books and checks they agree after every step. If the real book swapped "oldest first" for "newest first", this test would catch it within a few dozen steps.
- **CI** runs every test with GCC, with Clang, and under AddressSanitizer and UndefinedBehaviorSanitizer, which catch memory bugs.

## Performance

See [BENCHMARKS.md](BENCHMARKS.md). Current median: **~150 ns per operation**; p99: **~650 ns** on a shared cloud VM.

We report percentiles, not averages. In trading the slow outliers matter most, because the one slow order is the one that misses the trade.

## Layout

```
cpp/include/exchange/   headers (types, order book)
cpp/src/                implementation
cpp/tests/              unit tests, reference book, randomized comparison test
cpp/bench/              Google Benchmark + latency percentile tool
.github/workflows/      CI
```
