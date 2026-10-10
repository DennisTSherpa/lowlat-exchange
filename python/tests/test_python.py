"""Tests for the Python side. Uses only the standard library (unittest).

Run from the repository root after building:
    PYTHONPATH=build/python python3 -m unittest discover -s python/tests -v
"""

import os
import sys
import unittest

HERE = os.path.dirname(__file__)
sys.path.insert(0, os.path.join(HERE, "..", "sim"))
sys.path.insert(0, os.path.join(HERE, "..", "backtest"))

import exchange_py as ex  # noqa: E402
from market_maker import MarketMaker, MarketMakerConfig, run_backtest  # noqa: E402
from simulator import IdGenerator, MarketSimulator, SimConfig  # noqa: E402


class BindingsTest(unittest.TestCase):
    def test_limit_orders_match_at_resting_price(self):
        book = ex.OrderBook()
        self.assertEqual(book.add_limit(1, ex.Side.Sell, 100, 5), [])
        trades = book.add_limit(2, ex.Side.Buy, 105, 3)
        self.assertEqual(len(trades), 1)
        self.assertEqual((trades[0].maker_id, trades[0].taker_id), (1, 2))
        self.assertEqual((trades[0].price, trades[0].quantity), (100, 3))
        self.assertEqual(book.volume_at(ex.Side.Sell, 100), 2)

    def test_empty_book_returns_none(self):
        book = ex.OrderBook()
        self.assertIsNone(book.best_bid())
        self.assertIsNone(book.best_ask())

    def test_market_order_and_cancel(self):
        book = ex.OrderBook()
        book.add_limit(1, ex.Side.Buy, 99, 4)
        book.add_limit(2, ex.Side.Buy, 98, 4)
        self.assertTrue(book.cancel(2))
        self.assertFalse(book.has_order(2))
        trades = book.add_market(3, ex.Side.Sell, 10)
        self.assertEqual(sum(t.quantity for t in trades), 4)
        self.assertEqual(book.order_count(), 0)


class SimulatorTest(unittest.TestCase):
    def run_sim(self, seed, steps=500):
        book = ex.OrderBook()
        sim = MarketSimulator(book, IdGenerator(), SimConfig(seed=seed))
        trades = []
        for _ in range(steps):
            trades += [(t.maker_id, t.taker_id, t.price, t.quantity) for t in sim.step()]
        return trades, book

    def test_same_seed_gives_same_trades(self):
        self.assertEqual(self.run_sim(seed=3)[0], self.run_sim(seed=3)[0])
        self.assertNotEqual(self.run_sim(seed=3)[0], self.run_sim(seed=4)[0])

    def test_builds_a_two_sided_book(self):
        trades, book = self.run_sim(seed=1)
        self.assertGreater(len(trades), 0)
        self.assertIsNotNone(book.best_bid())
        self.assertIsNotNone(book.best_ask())
        self.assertLess(book.best_bid(), book.best_ask())


class MarketMakerTest(unittest.TestCase):
    def test_accounting_for_a_round_trip(self):
        book = ex.OrderBook()
        ids = IdGenerator()
        book.add_limit(ids.next(), ex.Side.Buy, 95, 100)   # someone else's bid
        book.add_limit(ids.next(), ex.Side.Sell, 105, 100)  # someone else's ask
        mm = MarketMaker(book, ids, MarketMakerConfig(half_spread=2, size=5, skew=0))

        mm.requote(fair_price=100)  # mid is 100, so it quotes 98 / 102
        self.assertEqual(book.best_bid(), 98)
        self.assertEqual(book.best_ask(), 102)

        # A seller hits our bid, then a buyer lifts our ask.
        mm.on_trades(book.add_market(ids.next(), ex.Side.Sell, 5), fair_price=100)
        self.assertEqual(mm.inventory, 5)
        mm.on_trades(book.add_market(ids.next(), ex.Side.Buy, 5), fair_price=100)
        self.assertEqual(mm.inventory, 0)

        self.assertEqual(mm.cash, (102 - 98) * 5)  # earned the spread
        self.assertEqual(mm.edge_ticks, 2 * 5 + 2 * 5)
        self.assertEqual(mm.fills, 2)

    def test_skew_moves_quotes_against_inventory(self):
        book = ex.OrderBook()
        ids = IdGenerator()
        book.add_limit(ids.next(), ex.Side.Buy, 90, 100)
        book.add_limit(ids.next(), ex.Side.Sell, 110, 100)
        mm = MarketMaker(book, ids, MarketMakerConfig(half_spread=2, size=5, skew=0.5))
        mm.inventory = 10  # long 10 shares, so quotes shift down by 5 ticks
        mm.requote(fair_price=100)
        self.assertEqual(book.best_bid(), 93)
        self.assertEqual(book.best_ask(), 97)

    def test_backtest_is_repeatable(self):
        a = run_backtest(1000, SimConfig(seed=5), MarketMakerConfig())
        b = run_backtest(1000, SimConfig(seed=5), MarketMakerConfig())
        self.assertEqual(a.pnl_series, b.pnl_series)
        self.assertGreater(a.fills, 0)


if __name__ == "__main__":
    unittest.main()
