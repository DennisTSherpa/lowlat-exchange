"""A live web view of the exchange.

A background thread runs the market simulator and the market maker against
the C++ order book. A small web server (Python standard library only, no
extra packages) serves one page that polls for the current state a few
times a second, and accepts orders typed into the page.

Run it from the repository root after building:
    PYTHONPATH=build/python python3 python/web/server.py
then open http://127.0.0.1:8000
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import threading
import time
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "sim"))
sys.path.insert(0, os.path.join(HERE, "..", "backtest"))

import exchange_py as ex  # noqa: E402
from market_maker import TICK_DOLLARS, MarketMaker, MarketMakerConfig  # noqa: E402
from simulator import IdGenerator, MarketSimulator, SimConfig  # noqa: E402

LADDER_LEVELS = 10
# Latency histogram buckets, in nanoseconds (upper edges).
LATENCY_BUCKETS = [250, 500, 1_000, 2_000, 4_000, 8_000, 16_000]


class TimedBook:
    """Wraps the C++ book and times every call that changes it.

    The time is measured from Python, so it includes the cost of calling
    from Python into C++ (roughly a microsecond), not just the matching.
    """

    def __init__(self, book: ex.OrderBook, samples: deque) -> None:
        self._book = book
        self._samples = samples

    def _timed(self, method, *args):
        start = time.perf_counter_ns()
        result = method(*args)
        self._samples.append(time.perf_counter_ns() - start)
        return result

    def add_limit(self, *args):
        return self._timed(self._book.add_limit, *args)

    def add_market(self, *args):
        return self._timed(self._book.add_market, *args)

    def cancel(self, *args):
        return self._timed(self._book.cancel, *args)

    def __getattr__(self, name):
        # Read-only calls (best_bid, depth, ...) go straight through.
        return getattr(self._book, name)


class LiveMarket:
    """One book, one simulator, one market maker, plus orders from the page.

    A lock makes sure the simulation thread and web requests never touch the
    book at the same time.
    """

    def __init__(self, seed: int = 1) -> None:
        self.lock = threading.Lock()
        self.latency_ns: deque[int] = deque(maxlen=20_000)
        self.book = TimedBook(ex.OrderBook(), self.latency_ns)
        self.ids = IdGenerator()
        self.sim = MarketSimulator(self.book, self.ids, SimConfig(seed=seed))
        self.mm = MarketMaker(self.book, self.ids, MarketMakerConfig())
        self.steps = 0
        self.trades: deque[dict] = deque(maxlen=30)
        self.pnl_history: deque[float] = deque(maxlen=300)
        self.my_orders: set[int] = set()  # ids of orders entered on the page

        for _ in range(50):  # warm up so the book isn't empty on first load
            self.sim.step()

    def step(self) -> None:
        with self.lock:
            fair = self.sim.fair_price
            self.mm.requote(fair)
            trades = self.sim.step()
            self.mm.on_trades(trades, self.sim.fair_price)
            self._record(trades)
            self.steps += 1
            if self.steps % 10 == 0:
                self.pnl_history.append(round(self.mm.pnl_ticks(self.sim.fair_price) * TICK_DOLLARS, 2))

    def _record(self, trades) -> None:
        for t in trades:
            who = "market maker" if (t.maker_id in self.mm._my_orders or t.taker_id in self.mm._my_orders) else ""
            if t.maker_id in self.my_orders or t.taker_id in self.my_orders:
                who = "you"
            self.trades.appendleft({"step": self.steps, "price": t.price, "quantity": t.quantity, "who": who})

    def submit(self, order: dict) -> dict:
        """Places an order typed into the page. Returns what happened."""
        side = {"buy": ex.Side.Buy, "sell": ex.Side.Sell}.get(order.get("side"))
        kind = order.get("type")
        try:
            quantity = int(order.get("quantity", 0))
            price = int(round(float(order.get("price", 0)) / TICK_DOLLARS)) if kind == "limit" else 0
        except (TypeError, ValueError):
            return {"ok": False, "error": "price and quantity must be numbers"}
        if side is None or kind not in ("limit", "market"):
            return {"ok": False, "error": "side must be buy or sell, type must be limit or market"}
        if not 1 <= quantity <= 1000:
            return {"ok": False, "error": "quantity must be between 1 and 1000"}
        if kind == "limit" and price <= 0:
            return {"ok": False, "error": "limit orders need a positive price"}

        with self.lock:
            order_id = self.ids.next()
            self.my_orders.add(order_id)
            if kind == "limit":
                trades = self.book.add_limit(order_id, side, price, quantity)
            else:
                trades = self.book.add_market(order_id, side, quantity)
            self.mm.on_trades(trades, self.sim.fair_price)
            self._record(trades)
            filled = sum(t.quantity for t in trades)
            resting = self.book.has_order(order_id)
        return {"ok": True, "order_id": order_id, "filled": filled, "trades": len(trades), "resting": resting}

    def snapshot(self) -> dict:
        with self.lock:
            bid, ask = self.book.best_bid(), self.book.best_ask()
            samples = sorted(self.latency_ns)
            return {
                "step": self.steps,
                "tick_dollars": TICK_DOLLARS,
                "fair_price": round(self.sim.fair_price, 2),
                "best_bid": bid,
                "best_ask": ask,
                "orders_in_book": self.book.order_count(),
                "bids": self.book.depth(ex.Side.Buy, LADDER_LEVELS),
                "asks": self.book.depth(ex.Side.Sell, LADDER_LEVELS),
                "trades": list(self.trades),
                "market_maker": {
                    "position": self.mm.inventory,
                    "pnl": round(self.mm.pnl_ticks(self.sim.fair_price) * TICK_DOLLARS, 2),
                    "fills": self.mm.fills,
                },
                "pnl_history": list(self.pnl_history),
                "latency": latency_summary(samples),
            }


def latency_summary(sorted_ns: list[int]) -> dict:
    if not sorted_ns:
        return {"count": 0, "p50": None, "p99": None, "histogram": []}

    def pct(p: float) -> int:
        return sorted_ns[int(p / 100 * (len(sorted_ns) - 1))]

    counts = [0] * (len(LATENCY_BUCKETS) + 1)
    for ns in sorted_ns:
        index = next((i for i, edge in enumerate(LATENCY_BUCKETS) if ns < edge), len(LATENCY_BUCKETS))
        counts[index] += 1
    labels = [f"< {edge / 1000:g} µs" for edge in LATENCY_BUCKETS] + [f"≥ {LATENCY_BUCKETS[-1] / 1000:g} µs"]
    return {
        "count": len(sorted_ns),
        "p50": pct(50),
        "p99": pct(99),
        "histogram": [{"label": label, "count": count} for label, count in zip(labels, counts)],
    }


def run_simulation(market: LiveMarket, steps_per_second: int, stop: threading.Event) -> None:
    interval = 0.05
    per_tick = max(1, round(steps_per_second * interval))
    while not stop.is_set():
        for _ in range(per_tick):
            market.step()
        stop.wait(interval)


STATIC_FILES = {
    "/": ("index.html", "text/html; charset=utf-8"),
    "/app.js": ("app.js", "text/javascript; charset=utf-8"),
}


def make_handler(market: LiveMarket):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == "/api/state":
                return self._send_json(market.snapshot())
            if self.path in STATIC_FILES:
                name, content_type = STATIC_FILES[self.path]
                with open(os.path.join(HERE, "static", name), "rb") as f:
                    return self._send(200, f.read(), content_type)
            self._send(404, b"not found", "text/plain")

        def do_POST(self):
            if self.path != "/api/order":
                return self._send(404, b"not found", "text/plain")
            length = int(self.headers.get("Content-Length", 0))
            try:
                order = json.loads(self.rfile.read(min(length, 10_000)) or b"{}")
            except json.JSONDecodeError:
                return self._send_json({"ok": False, "error": "body must be JSON"}, 400)
            result = market.submit(order if isinstance(order, dict) else {})
            self._send_json(result, 200 if result["ok"] else 400)

        def _send_json(self, data, status=200):
            self._send(status, json.dumps(data).encode(), "application/json")

        def _send(self, status, body, content_type):
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):  # keep the terminal quiet
            pass

    return Handler


def main() -> None:
    parser = argparse.ArgumentParser(description="Live web view of the exchange")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--speed", type=int, default=100, help="simulation steps per second")
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()

    market = LiveMarket(seed=args.seed)
    stop = threading.Event()
    threading.Thread(target=run_simulation, args=(market, args.speed, stop), daemon=True).start()

    server = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(market))
    print(f"open http://127.0.0.1:{args.port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        server.server_close()


if __name__ == "__main__":
    main()
