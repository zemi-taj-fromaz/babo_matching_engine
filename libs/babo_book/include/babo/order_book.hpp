// --- babo_book -------------------------------------------------------------
// YOUR matching engine. This is a placeholder skeleton so the build graph and
// the benchmark harness compile end-to-end from day one. Replace the bodies in
// src/order_book.cpp (and reshape this interface) as you design the real thing.
//
// The signature below is deliberately minimal and mirrors the workload the
// bench drives liquibook with, so the two libs can be compared apples-to-apples.
// Change it freely -- just keep bench/bench_main.cpp in sync.
#pragma once

#include <cstdint>
#include <cstddef>

namespace babo {

using OrderId  = std::uint32_t;
using Price    = std::uint32_t;
using Quantity = std::uint32_t;

class OrderBook {
public:
    OrderBook();
    ~OrderBook();

    // Submit a limit order; matching against the opposite side happens here.
    void add(OrderId id, bool is_buy, Price price, Quantity qty);

    // Cancel a resting order by id (no-op if already filled/cancelled).
    void cancel(OrderId id);

    // Number of orders still resting on the book (bids + asks).
    // Used by the bench to report "orders matched" with parity to liquibook.
    std::size_t resting_count() const;
};

} // namespace babo
