"""Tests for the live web view: the market state and the HTTP endpoints.

Run from the repository root after building:
    PYTHONPATH=build/python python3 -m unittest discover -s python/tests -v
"""

import json
import os
import sys
import threading
import unittest
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "web"))

from server import LiveMarket, latency_summary, make_handler  # noqa: E402


class LiveMarketTest(unittest.TestCase):
    def test_snapshot_has_a_sorted_two_sided_ladder(self):
        market = LiveMarket(seed=2)
        for _ in range(200):
            market.step()
        state = market.snapshot()
        bids = [price for price, _ in state["bids"]]
        asks = [price for price, _ in state["asks"]]
        self.assertTrue(bids and asks)
        self.assertEqual(bids, sorted(bids, reverse=True))  # best (highest) bid first
        self.assertEqual(asks, sorted(asks))                 # best (lowest) ask first
        self.assertLess(bids[0], asks[0])
        self.assertGreater(state["latency"]["count"], 0)

    def test_book_size_stays_bounded(self):
        market = LiveMarket(seed=3)
        for _ in range(3000):
            market.step()
        self.assertLess(market.snapshot()["orders_in_book"], 400)

    def test_submit_market_order_fills_and_is_tagged(self):
        market = LiveMarket(seed=4)
        result = market.submit({"side": "buy", "type": "market", "quantity": 3})
        self.assertTrue(result["ok"])
        self.assertEqual(result["filled"], 3)
        self.assertFalse(result["resting"])
        self.assertEqual(market.snapshot()["trades"][0]["who"], "you")

    def test_submit_rejects_bad_orders(self):
        market = LiveMarket(seed=5)
        bad = [
            {"side": "up", "type": "limit", "quantity": 1, "price": 100},
            {"side": "buy", "type": "stop", "quantity": 1},
            {"side": "buy", "type": "limit", "quantity": 0, "price": 100},
            {"side": "buy", "type": "limit", "quantity": 1, "price": "abc"},
            {"side": "buy", "type": "limit", "quantity": 1, "price": -5},
        ]
        for order in bad:
            self.assertFalse(market.submit(order)["ok"], order)

    def test_latency_summary(self):
        summary = latency_summary(sorted([100, 300, 300, 900, 50_000]))
        self.assertEqual(summary["count"], 5)
        self.assertEqual(summary["p50"], 300)
        counts = [bucket["count"] for bucket in summary["histogram"]]
        self.assertEqual(counts, [1, 2, 1, 0, 0, 0, 0, 1])


class HttpTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(LiveMarket(seed=6)))
        cls.url = f"http://127.0.0.1:{cls.server.server_address[1]}"
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def post(self, body: bytes):
        request = urllib.request.Request(self.url + "/api/order", data=body, method="POST")
        try:
            with urllib.request.urlopen(request) as response:
                return response.status, json.load(response)
        except urllib.error.HTTPError as error:
            return error.code, json.load(error)

    def test_page_and_state(self):
        with urllib.request.urlopen(self.url + "/") as response:
            self.assertIn(b"Low-Latency Exchange", response.read())
        with urllib.request.urlopen(self.url + "/api/state") as response:
            self.assertIn("bids", json.load(response))

    def test_order_endpoint(self):
        status, result = self.post(b'{"side": "sell", "type": "limit", "quantity": 2, "price": "50.00"}')
        self.assertEqual(status, 200)
        self.assertTrue(result["ok"])
        self.assertEqual(result["filled"], 2)  # far below the market, so it trades at once

        status, result = self.post(b"not json")
        self.assertEqual(status, 400)
        self.assertFalse(result["ok"])

    def test_unknown_path(self):
        with self.assertRaises(urllib.error.HTTPError) as context:
            urllib.request.urlopen(self.url + "/secret")
        self.assertEqual(context.exception.code, 404)


if __name__ == "__main__":
    unittest.main()
