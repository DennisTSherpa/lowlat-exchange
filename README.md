# Low-Latency Exchange

A stock-exchange-style matching engine in C++, built step by step, with every latency claim backed by a benchmark you can rerun.

The rule for this project is **simplest version first**. Each part starts with plain standard-library code that someone with foundation knowledge of C++ can follow. An optimization only goes in when a benchmark shows it helps, and the README explains it in plain words.

## Status

| Milestone | What | State |
|-----------|------|-------|
| 1 | Order book: add, cancel, match, with tests and benchmarks | ✅ done |
| 2 | Matching engine: market orders, event log, replay, 10 ms worst case fixed | ✅ done |
| 3 | Order entry over TCP, end-to-end latency | ✅ done |
| 4 | Python: bindings, market simulator, backtest | ✅ done |
| 5 | Web view of the live book | next |

## Build and test

You need a Linux machine with CMake 3.20+, Ninja, GCC 12+ or Clang 15+, and Python 3.9+ with its development headers (`python3-dev` on Ubuntu).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/latency
./build/record_session session.bin && ./build/replay_session session.bin

# end to end over TCP (two terminals)
./build/exchange_server 9000 server.bin
./build/load_client 9000 200000
```

GoogleTest, Google Benchmark and pybind11 are downloaded automatically the first time you run CMake.

```bash
# Python tests and the market-making backtest
PYTHONPATH=build/python python3 -m unittest discover -s python/tests -v
PYTHONPATH=build/python python3 python/backtest/market_maker.py
```

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
- **Market orders** use the same matching loop, but any price counts as crossing, and whatever quantity is left over is dropped instead of resting.
- **Room is reserved up front** in the id index, so it never has to resize in the middle of trading. Resizing once took 10 ms; see [BENCHMARKS.md](BENCHMARKS.md#milestone-2-no-hash-table-resize-during-trading) for how that was found.

## Matching engine and event log

```
event (add limit / add market / cancel)
   │
   ▼
MatchingEngine::process ──► append to event log file (22 bytes per event)
   │
   ▼
OrderBook ──► trades
```

- [`MatchingEngine`](cpp/include/exchange/matching_engine.hpp) is the single entry point. It writes each event to the log *before* applying it, so the log is a complete record of the session.
- The engine is **deterministic**. It has no clocks, no randomness and no threads, so the same events always produce the same trades.
- That means any session can be **replayed** from its log, so a bug seen once can be reproduced as many times as needed. `record_session` and `replay_session` show this: both print a fingerprint of every trade, and the two fingerprints match. CI checks this on every push.
- The log format ([`event_log.hpp`](cpp/include/exchange/event_log.hpp)) is fixed-size binary records: type, side, id, price, quantity. There's nothing to parse; the reader just copies the bytes back.

## Order entry over TCP

```
load_client ──22-byte request──► exchange_server ──► MatchingEngine ──► OrderBook
            ◄──17-byte response──
```

- **Messages are fixed-size binary** ([`wire.hpp`](cpp/include/exchange/wire.hpp)). A request is 22 bytes (type, side, id, price, quantity) and a response is 17 bytes (accepted or rejected, id, filled quantity, number of trades). There's no text parsing or JSON. The request is the exact same 22-byte record the event log stores, so a server's log is a recording of what it received.
- **The server** ([`tcp.cpp`](cpp/src/tcp.cpp)) uses plain blocking POSIX sockets and serves one client at a time. It reads a request, applies it, and sends exactly one response.
- **TCP is a stream, not a list of messages.** One `read()` may return half a message, or one and a half. `read_exact` loops until a whole message has arrived. This is the most common bug in hand-written network code.
- **`TCP_NODELAY`** turns off Nagle's algorithm, which would otherwise hold small messages back for a short while to batch them up.
- **Invalid requests are rejected:** unknown message types or sides, zero quantity, duplicate ids, and cancels of orders that aren't in the book.

## Python: simulator and market-making backtest

```
Python simulator + strategy ──pybind11──► the same C++ OrderBook
```

- **Bindings** ([`bindings.cpp`](python/bindings/bindings.cpp)) expose the C++ `OrderBook` to Python as the `exchange_py` module. Python uses the exact same matching code as the server, so there's only one implementation to trust.
- **Simulator** ([`simulator.py`](python/sim/simulator.py)): a hidden *fair price* moves randomly. Liquidity providers place limit orders a few ticks either side of it and cancel old ones, and noise traders send small market orders in random directions. One seed controls everything, so a run repeats exactly.
- **Market maker** ([`market_maker.py`](python/backtest/market_maker.py)): it keeps one bid and one ask around the mid price and earns the spread when others trade with it. **Inventory skew** shifts both quotes against its position: long means quote lower, so it sells more and buys less.
- **Edge per share** measures, for every fill, how far the trade price was from the fair price in the market maker's favour. It's a cleaner signal than profit, which swings with whatever inventory happens to be held.

### Results

`python3 python/backtest/market_maker.py` runs 20,000 steps per run and averages 10 seeds. "±" is the spread (standard deviation) across seeds. A fast market means the fair price moves twice as fast per step.

| Scenario | PnL ($) | Edge per share (ticks) | Fills | Max position |
|----------|---------|------------------------|-------|--------------|
| calm market, no skew | 8.62 ± 8.30 | 0.50 ± 0.34 | 671 | 54 |
| calm market, skew | 13.29 ± 3.37 | 0.78 ± 0.15 | 510 | 38 |
| fast market, no skew | -35.35 ± 12.30 | -0.69 ± 0.41 | 1,124 | 54 |
| fast market, skew | -0.14 ± 9.45 | 0.03 ± 0.18 | 1,169 | 54 |

What this shows:
- **In a calm market, making markets pays.** The market maker earns about half a tick per share.
- **In a fast market it loses money. This is adverse selection.** When the price moves before the quotes are updated, the providers' fresh orders trade against the stale quotes, so the market maker sells just before prices rise and buys just before they fall. Edge per share turns negative. This is the main reason trading firms care about speed: the faster you can update quotes, the less of this you suffer.
- **Skew helps in both markets.** It raises profit, cuts the run-to-run spread, and in a fast market turns a clear loss into roughly break-even.

## How we know it's correct

- **Unit tests** ([`order_book_test.cpp`](cpp/tests/order_book_test.cpp)) cover each rule on its own: priority, partial fills, sweeping several levels, and cancels.
- **Randomized comparison** (limit orders, market orders and cancels) ([`random_compare_test.cpp`](cpp/tests/random_compare_test.cpp)): a slow [reference book](cpp/tests/reference_book.hpp) that just scans a flat list is easy to check by eye. The test sends 100,000 random orders and cancels to both books and checks they agree after every step. If the real book swapped "oldest first" for "newest first", this test would catch it within a few dozen steps.
- **Replay test** ([`engine_test.cpp`](cpp/tests/engine_test.cpp)) records 20,000 random events, replays them into a fresh engine, and checks that the trades and the final book are identical.
- **Network tests** ([`tcp_test.cpp`](cpp/tests/tcp_test.cpp)) start a real server on a random local port, talk to it with the client, and check every response and the final book.
- **Python tests** ([`test_python.py`](python/tests/test_python.py)) check the bindings, that the simulator repeats exactly for a given seed, and the market maker's cash and inventory accounting on a hand-worked example.
- **CI** runs every test with GCC, with Clang, and under AddressSanitizer and UndefinedBehaviorSanitizer, which catch memory bugs.

## Performance

See [BENCHMARKS.md](BENCHMARKS.md). On a shared cloud VM, the current median is **~115 ns per operation**, p99 is **~500 ns**, and the worst case is **under 2 ms** (down from 10 ms). A full round trip over TCP takes **~30 µs**, and almost all of that is the operating system, not the matching.

We report percentiles, not averages. In trading the slow outliers matter most, because the one slow order is the one that misses the trade.

## Layout

```
cpp/include/exchange/   headers (types, order book, event log, matching engine, wire format, TCP)
cpp/src/                implementation
cpp/tests/              unit tests, reference book, randomized comparison test
cpp/bench/              Google Benchmark + latency percentile tool
cpp/tools/              exchange_server, load_client, record_session, replay_session
python/bindings/        pybind11 module exposing the order book
python/sim/             market simulator
python/backtest/        market-making strategy and backtest
python/tests/           Python tests (standard-library unittest)
.github/workflows/      CI
```
