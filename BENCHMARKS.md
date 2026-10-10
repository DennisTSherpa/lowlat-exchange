# Benchmarks

All numbers come from the commands below, so anyone can rerun them. Latency numbers depend heavily on the machine, so always compare runs on the same machine.

## How to reproduce

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/latency            # per-operation latency percentiles, 1,000,000 actions
./build/order_book_bench   # Google Benchmark throughput
```

The workload is fixed by a random seed (see `cpp/bench/workload.hpp`). It is about 70% new limit orders within ±50 ticks of a mid price and 30% cancels of recent orders. Roughly half the orders trade.

## Milestone 1: standard-library order book

**Machine:** shared 4-core cloud VM, Intel Xeon @ 2.10 GHz, GCC 13.3, `-O3` Release build, no CPU pinning or tuning.

### Latency per operation (`./build/latency`)

Each sample includes about 29 ns spent reading the clock.

| p50 | p90 | p99 | p99.9 | max |
|-----|-----|-----|-------|-----|
| 150 ns | 320 ns | 649 ns | 2,457 ns | 9.7 ms |

Three repeated runs gave a p50 of 148–203 ns and a p99 of 648–891 ns.

### Throughput (`./build/order_book_bench`)

| Benchmark | Result |
|-----------|--------|
| Add 1,000 orders that don't trade | 7.5 M orders/s |
| Mixed workload, 100,000 actions | 7.4 M actions/s |

### What these numbers say

- The typical operation takes a few hundred nanoseconds, which is fine for a first version.
- **The max (about 10 ms, the same on every run) is the interesting part.** It's a single operation that runs tens of thousands of times slower than usual. The suspected cause is the `std::unordered_map` of order ids resizing itself (rehashing) as the book grows. A shared VM pausing the process is another possibility. Milestone 2 starts by confirming the cause and then fixing it.
- Every add or cancel allocates memory (a `std::list` node, a map node, and the returned `std::vector`). Removing those allocations is the next planned optimization, measured against this table.
