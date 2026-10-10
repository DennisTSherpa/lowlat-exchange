// Python bindings for the C++ order book, built with pybind11.
// Python code uses the exact same matching logic as the server, so there is
// only one implementation to test and trust.
//
//   import exchange_py as ex
//   book = ex.OrderBook()
//   book.add_limit(1, ex.Side.Sell, 10_000, 5)
//   trades = book.add_limit(2, ex.Side.Buy, 10_000, 3)

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "exchange/order_book.hpp"

namespace py = pybind11;
using namespace exchange;

PYBIND11_MODULE(exchange_py, m) {
    m.doc() = "Price-time priority limit order book (C++)";

    py::enum_<Side>(m, "Side").value("Buy", Side::Buy).value("Sell", Side::Sell);

    py::class_<Trade>(m, "Trade")
        .def_readonly("maker_id", &Trade::maker_id)
        .def_readonly("taker_id", &Trade::taker_id)
        .def_readonly("price", &Trade::price)
        .def_readonly("quantity", &Trade::quantity)
        .def("__repr__", [](const Trade& t) {
            return "Trade(maker_id=" + std::to_string(t.maker_id) +
                   ", taker_id=" + std::to_string(t.taker_id) +
                   ", price=" + std::to_string(t.price) +
                   ", quantity=" + std::to_string(t.quantity) + ")";
        });

    py::class_<OrderBook>(m, "OrderBook")
        // A smaller default than C++: simulations rarely hold a million orders,
        // and many small books should not each reserve 8 MB.
        .def(py::init<std::size_t>(), py::arg("expected_orders") = 100'000)
        .def(
            "add_limit",
            [](OrderBook& book, OrderId id, Side side, Price price, Quantity quantity) {
                return book.add_limit(Order{id, side, price, quantity});
            },
            py::arg("order_id"), py::arg("side"), py::arg("price"), py::arg("quantity"),
            "Match against the other side, rest any remainder. Returns the trades.")
        .def(
            "add_market",
            [](OrderBook& book, OrderId id, Side side, Quantity quantity) {
                return book.add_market(Order{id, side, 0, quantity});
            },
            py::arg("order_id"), py::arg("side"), py::arg("quantity"),
            "Trade at any price until filled; never rests. Returns the trades.")
        .def("cancel", &OrderBook::cancel, py::arg("order_id"))
        .def("best_bid", &OrderBook::best_bid, "Highest bid price, or None")
        .def("best_ask", &OrderBook::best_ask, "Lowest ask price, or None")
        .def("volume_at", &OrderBook::volume_at, py::arg("side"), py::arg("price"))
        .def("has_order", &OrderBook::has_order, py::arg("order_id"))
        .def("order_count", &OrderBook::order_count);
}
