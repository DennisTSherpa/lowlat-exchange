"""A simple market simulator that sends order flow into the C++ order book.

There is a hidden "fair price" that moves randomly (a random walk). Two
kinds of simulated traders react to it:

  * Liquidity providers place limit orders a few ticks around the fair price
    and later cancel some of them. They fill the book. Because they always
    use the latest fair price, their new orders trade against anyone still
    quoting an old price.
  * Noise traders send small market orders in random directions.

Everything is driven by one random seed, so a run can be repeated exactly.
"""

from __future__ import annotations

import random
from dataclasses import dataclass

import exchange_py as ex


class IdGenerator:
    """Hands out unique order ids shared by everyone using the same book."""

    def __init__(self) -> None:
        self._next = 1

    def next(self) -> int:
        order_id = self._next
        self._next += 1
        return order_id


@dataclass
class SimConfig:
    seed: int = 1
    start_price: int = 10_000          # in ticks; with a $0.01 tick this is $100.00
    volatility: float = 0.3            # standard deviation of the fair price move per step, in ticks
    limit_orders_per_step: int = 4     # liquidity provider orders per step
    min_distance: int = 3              # providers quote between min_distance and max_distance
    max_distance: int = 10             #   ticks away from fair, leaving room inside the spread
    cancels_per_step: int = 3          # old provider orders cancelled per step (keeps the book a steady size)
    noise_probability: float = 0.30    # chance per step of a noise market order
    max_quantity: int = 10


class MarketSimulator:
    def __init__(self, book: ex.OrderBook, ids: IdGenerator, config: SimConfig) -> None:
        self.book = book
        self.ids = ids
        self.config = config
        self.rng = random.Random(config.seed)
        self.fair_price = float(config.start_price)
        self._resting: list[int] = []  # ids of provider orders that may still rest

    def fair_tick(self) -> int:
        return round(self.fair_price)

    def step(self) -> list[ex.Trade]:
        """Moves the fair price, then sends one step of order flow.

        Returns every trade that happened, so other participants (like a
        market maker) can see whether their orders were filled.
        """
        cfg = self.config
        rng = self.rng
        trades: list[ex.Trade] = []

        self.fair_price += rng.gauss(0.0, cfg.volatility)
        fair = self.fair_tick()

        # Liquidity providers: buy orders below fair, sell orders above.
        for _ in range(cfg.limit_orders_per_step):
            side = ex.Side.Buy if rng.random() < 0.5 else ex.Side.Sell
            distance = rng.randint(cfg.min_distance, cfg.max_distance)
            price = fair - distance if side == ex.Side.Buy else fair + distance
            order_id = self.ids.next()
            trades += self.book.add_limit(order_id, side, price, rng.randint(1, cfg.max_quantity))
            if self.book.has_order(order_id):
                self._resting.append(order_id)

        # Providers cancel a few old orders, so stale prices get cleaned up.
        # Some of these ids were already filled; cancel() then just returns False.
        for _ in range(min(cfg.cancels_per_step, len(self._resting))):
            index = rng.randrange(len(self._resting))
            self.book.cancel(self._resting.pop(index))

        # Noise traders: market orders in a random direction.
        if rng.random() < cfg.noise_probability:
            side = ex.Side.Buy if rng.random() < 0.5 else ex.Side.Sell
            trades += self.book.add_market(self.ids.next(), side, rng.randint(1, cfg.max_quantity))

        return trades
