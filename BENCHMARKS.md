# Benchmarks

All numbers come from the commands below, so anyone can rerun them. Latency numbers depend heavily on the machine, so always compare runs on the same machine.

## How to reproduce

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/latency            # per-operation latency percentiles, 1,000,000 actions
./build/order_book_bench   # Google Benchmark throughput

# end to end over TCP (two terminals)
./build/exchange_server 9000
./build/load_client 9000 200000
```

The workload is fixed by a random seed (see `cpp/bench/workload.hpp`). It is about 70% new limit orders within ±50 ticks of a mid price and 30% cancels of recent orders. Roughly half the orders trade.

## Milestone 3: end to end over TCP

**Same machine.** Server and client run as two processes on the same VM and talk over loopback (127.0.0.1). The client sends one request, waits for the response, then sends the next. Each sample is a full round trip: client write → kernel → server read → match → server write → kernel → client read.

| Run | p50 | p90 | p99 | p99.9 | max |
|-----|-----|-----|-----|-------|-----|
| 1 | 30.9 µs | 38.8 µs | 70.4 µs | 234 µs | 4.75 ms |
| 2 | 29.6 µs | 38.4 µs | 65.1 µs | 170 µs | 6.45 ms |
| 3 | 30.0 µs | 38.4 µs | 71.9 µs | 250 µs | 4.11 ms |

200,000 requests per run. The server logged every request, and replaying each log gave the same 108,848 trades every time.

### What these numbers say

- **Matching is under 0.5% of the round trip.** The order book takes ~0.1 µs; the whole trip takes ~30 µs. Almost all of the time goes to the operating system: four system calls per round trip (two writes, two reads), and waking up a sleeping process each time a message arrives.
- This is why real trading systems spend so much effort on the network path (busy-polling instead of sleeping, pinning threads to cores, and eventually kernel-bypass networking), and comparatively little on the book itself.
- A simple next experiment is busy-polling. The receiver keeps checking the socket in a loop instead of sleeping in `read()`, which removes the wake-up cost at the price of one fully busy CPU core per side.

## Milestone 2: no hash table resize during trading

**Same machine and settings as milestone 1.** Each row of the latency table is the old and new binary run back to back, three times over.

### Finding the cause

Milestone 1 showed a worst case of about 10 ms on every run. Logging the slowest operations together with the book size at that moment gave:

| Slow operation | Resting orders at that moment |
|---|---|
| 10.6 ms | 85,230 |
| 3.2 ms | 42,044 |
| 0.8 ms | 20,754 |
| 0.2 ms | 10,274 |

The sizes roughly double each time. A small test program printed the exact sizes at which `std::unordered_map` (GCC's standard library) grows its table: **5,088, 10,274, 20,754, 42,044 and 85,230**. They match exactly. Each growth step ("rehash") allocates a bigger table and moves every entry across, all inside one unlucky `add_limit` call.

### The fix

One line: the `OrderBook` constructor calls `locations_.reserve(expected_orders)` (default 1,000,000), so the table is big enough from the start and never grows while trading.

### Latency per operation, before and after

| Run | p50 | p90 | p99 | p99.9 | max |
|-----|-----|-----|-----|-------|-----|
| before | 152 ns | 346 ns | 729 ns | 2,702 ns | 9.97 ms |
| after  | 111 ns | 212 ns | 478 ns | 3,027 ns | 0.13 ms |
| before | 147 ns | 326 ns | 677 ns | 2,328 ns | 11.4 ms |
| after  | 117 ns | 235 ns | 563 ns | 3,695 ns | 0.72 ms |
| before | 162 ns | 385 ns | 813 ns | 4,144 ns | 10.1 ms |
| after  | 114 ns | 228 ns | 515 ns | 2,287 ns | 1.73 ms |

- **The max dropped from ~10 ms to 0.1–1.7 ms**, and the cliff no longer grows with the book.
- The median and p99 also improved by about 25–30%, probably because entries no longer get moved around in memory.
- p99.9 didn't change much. The remaining spikes vary from run to run, which points to the machine (a shared cloud VM) or to memory allocation in `std::map` and `std::list`, and not to the hash table. That's the next thing to investigate.
- **Trade-off:** reserving room for 1,000,000 orders costs about 8 MB of memory up front, even for an empty book.

### Throughput (`./build/order_book_bench`)

| Benchmark | Result |
|-----------|--------|
| Add 1,000 orders that don't trade | 13.4–14.0 M orders/s |
| Mixed workload, 100,000 actions | 10.3–11.3 M actions/s |

These numbers aren't directly comparable with milestone 1's. The benchmark now pauses the timer while the book is destroyed, because freeing the reserved table isn't part of trading.

## Determinism check

```bash
./build/record_session session.bin 1000000   # run the workload, log every event
./build/replay_session session.bin           # rebuild everything from the log
```

Both print the same summary, including a fingerprint (a hash) of every trade:

```
events:            1000000
trades:            541783
resting orders:    90890
best bid / ask:    9971 / 10018
trade fingerprint: fc48cffe340654e4
```

The log is 22 bytes per event, so 22 MB for a million events. CI runs this check on every push.

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
