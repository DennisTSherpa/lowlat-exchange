"""A basic market-making strategy and a backtest that runs it against the
simulator, using the real C++ order book.

The market maker always tries to have one buy order (bid) and one sell order
(ask) in the book, a little below and above the current mid price. It earns
the gap (the "spread") when other traders buy from it and sell to it.

It has two risks:
  * Inventory: if it keeps buying and the price falls, it loses money. To
    limit this, it "skews" its quotes: holding too much makes it quote lower,
    so it is more likely to sell and less likely to buy.
  * Adverse selection: when the price moves before it can update its quotes,
    the trades it gets are the bad ones (it sells just before the price goes
    up). The faster the market moves, the worse this gets.

Run it:
    PYTHONPATH=build/python python3 python/backtest/market_maker.py
"""

from __future__ import annotations

import argparse
import math
import os
import statistics
import sys
from dataclasses import dataclass, field

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "sim"))

import exchange_py as ex  # noqa: E402
from simulator import IdGenerator, MarketSimulator, SimConfig  # noqa: E402

TICK_DOLLARS = 0.01  # one price tick is one cent


@dataclass
class MarketMakerConfig:
    half_spread: float = 1.0   # quote this many ticks either side of the reference price
    size: int = 5              # quantity per quote
    max_inventory: int = 50    # stop buying (or selling) beyond this position
    skew: float = 0.1          # ticks to shift quotes per unit of inventory (0 = no skew)


@dataclass
class MarketMaker:
    book: ex.OrderBook
    ids: IdGenerator
    config: MarketMakerConfig
    inventory: int = 0         # shares held; negative means short
    cash: int = 0              # in ticks
    fills: int = 0
    volume: int = 0
    max_abs_inventory: int = 0
    # Measurement only (the strategy never uses it to decide anything):
    # for each fill, how far the price was from the true fair price, in our
    # favour. Positive means we bought below fair or sold above it.
    edge_ticks: float = 0.0
    _my_orders: dict[int, ex.Side] = field(default_factory=dict)
    _last_mid: float | None = None

    def on_trades(self, trades: list[ex.Trade], fair_price: float) -> None:
        """Updates inventory and cash for every trade that involved our orders."""
        for trade in trades:
            for order_id in (trade.maker_id, trade.taker_id):
                side = self._my_orders.get(order_id)
                if side is None:
                    continue
                if side == ex.Side.Buy:
                    self.inventory += trade.quantity
                    self.cash -= trade.price * trade.quantity
                    self.edge_ticks += (fair_price - trade.price) * trade.quantity
                else:
                    self.inventory -= trade.quantity
                    self.cash += trade.price * trade.quantity
                    self.edge_ticks += (trade.price - fair_price) * trade.quantity
                self.fills += 1
                self.volume += trade.quantity
                self.max_abs_inventory = max(self.max_abs_inventory, abs(self.inventory))

    def requote(self, fair_price: float) -> None:
        """Cancels the old quotes and places new ones around the mid price."""
        for order_id in list(self._my_orders):
            if self.book.has_order(order_id):
                self.book.cancel(order_id)
        self._my_orders.clear()

        bid, ask = self.book.best_bid(), self.book.best_ask()
        if bid is not None and ask is not None:
            self._last_mid = (bid + ask) / 2
        if self._last_mid is None:
            return  # nothing to price off yet

        cfg = self.config
        reference = self._last_mid - cfg.skew * self.inventory
        if self.inventory < cfg.max_inventory:
            self._place(ex.Side.Buy, math.floor(reference - cfg.half_spread), fair_price)
        if self.inventory > -cfg.max_inventory:
            self._place(ex.Side.Sell, math.ceil(reference + cfg.half_spread), fair_price)

    def _place(self, side: ex.Side, price: int, fair_price: float) -> None:
        order_id = self.ids.next()
        self._my_orders[order_id] = side
        # The quote can trade straight away if the market has moved through it.
        self.on_trades(self.book.add_limit(order_id, side, price, self.config.size), fair_price)

    def pnl_ticks(self, mark_price: float) -> float:
        """Profit so far, valuing current inventory at mark_price."""
        return self.cash + self.inventory * mark_price


@dataclass
class BacktestResult:
    pnl_dollars: float
    edge_per_share: float  # average ticks gained (or lost) against fair per share traded
    fills: int
    volume: int
    final_inventory: int
    max_abs_inventory: int
    pnl_series: list[float]


def run_backtest(steps: int, sim_config: SimConfig, mm_config: MarketMakerConfig) -> BacktestResult:
    book = ex.OrderBook()
    ids = IdGenerator()
    sim = MarketSimulator(book, ids, sim_config)
    mm = MarketMaker(book, ids, mm_config)

    # Let the providers build up a book before the market maker starts.
    for _ in range(50):
        sim.step()

    pnl_series = []
    for _ in range(steps):
        mm.requote(sim.fair_price)
        trades = sim.step()  # the fair price moves first, then the order flow arrives
        mm.on_trades(trades, sim.fair_price)
        pnl_series.append(mm.pnl_ticks(sim.fair_price) * TICK_DOLLARS)

    return BacktestResult(
        pnl_dollars=pnl_series[-1],
        edge_per_share=mm.edge_ticks / mm.volume if mm.volume else 0.0,
        fills=mm.fills,
        volume=mm.volume,
        final_inventory=mm.inventory,
        max_abs_inventory=mm.max_abs_inventory,
        pnl_series=pnl_series,
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--steps", type=int, default=20_000)
    parser.add_argument("--seeds", type=int, default=10, help="number of random seeds to average over")
    args = parser.parse_args()

    # (name, fair price volatility per step in ticks, inventory skew)
    scenarios = [
        ("calm market, no skew", 0.3, 0.0),
        ("calm market, skew", 0.3, 0.1),
        ("fast market, no skew", 0.6, 0.0),
        ("fast market, skew", 0.6, 0.1),
    ]

    print(f"{args.steps:,} steps per run, average of {args.seeds} seeds (± is the spread across seeds)\n")
    print("| Scenario | PnL ($) | Edge per share (ticks) | Fills | Max position |")
    print("|----------|---------|------------------------|-------|--------------|")
    for name, volatility, skew in scenarios:
        results = [
            run_backtest(
                args.steps,
                SimConfig(seed=seed, volatility=volatility),
                MarketMakerConfig(skew=skew),
            )
            for seed in range(1, args.seeds + 1)
        ]
        pnl = [r.pnl_dollars for r in results]
        edge = [r.edge_per_share for r in results]
        print(
            f"| {name} | {statistics.mean(pnl):,.2f} ± {statistics.stdev(pnl):,.2f} "
            f"| {statistics.mean(edge):.2f} ± {statistics.stdev(edge):.2f} "
            f"| {statistics.mean(r.fills for r in results):,.0f} "
            f"| {statistics.mean(r.max_abs_inventory for r in results):.0f} |"
        )


if __name__ == "__main__":
    main()
