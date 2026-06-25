// --- bench_common ------------------------------------------------------------
// Shared harness for the standalone latency/throughput benchmark executables.
// Each benchmark exe links exactly ONE order book library and includes its
// <simple/simple_order_book.h>, then instantiates the templated runners below
// with that library's SimpleOrderBook / SimpleOrder types. Both libraries expose
// the same API (book.add(Order*), book.bids()/asks().size()), so one harness
// drives both.
//
// These are benchmarks, not unit tests: they report numbers, they don't assert.
#ifndef BABO_BENCH_COMMON_H
#define BABO_BENCH_COMMON_H

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

namespace bench {

using Clock = std::chrono::steady_clock;

inline double ns_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::nano>(b - a).count();
}

struct OrderSpec {
    bool          is_buy;
    std::uint32_t price;
    std::uint32_t qty;
};

// Deterministic workload (fixed seed) so every engine sees the exact same input
// and runs are reproducible. Mirrors the canonical liquibook perf/latency workload
// (src/perf_order_book.cpp): alternating buy/sell, buys priced 1880-1889 and sells
// 1884-1893 so the 1884-1889 overlap crosses and actually exercises matching.
inline std::vector<OrderSpec> make_workload(std::size_t n) {
    std::mt19937 rng(static_cast<unsigned>(n));
    std::uniform_int_distribution<std::uint32_t> spread(0, 9);

    std::vector<OrderSpec> w;
    w.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        bool is_buy = (i % 2) == 0;
        std::uint32_t price = spread(rng) + (is_buy ? 1880u : 1884u);
        std::uint32_t qty = (spread(rng) + 1) * 100;
        w.push_back({is_buy, price, qty});
    }
    return w;
}

struct Stats {
    std::size_t n = 0;
    double min = 0, p50 = 0, p90 = 0, p99 = 0, p999 = 0, max = 0, mean = 0, sum = 0;
};

// Takes the samples by value: sorts them in place.
inline Stats summarize(std::vector<double> lat) {
    std::sort(lat.begin(), lat.end());
    Stats s;
    s.n = lat.size();
    if (s.n == 0) return s;

    auto pct = [&](double p) { return lat[static_cast<std::size_t>(p * (lat.size() - 1))]; };
    s.min  = lat.front();
    s.max  = lat.back();
    s.p50  = pct(0.50);
    s.p90  = pct(0.90);
    s.p99  = pct(0.99);
    s.p999 = pct(0.999);
    for (double x : lat) s.sum += x;
    s.mean = s.sum / s.n;
    return s;
}

inline void report_throughput(const char* name, std::size_t n, double ms, std::size_t resting) {
    double per_sec = ms > 0.0 ? (n / (ms / 1000.0)) : 0.0;
    std::cout << "  " << name << "  throughput: " << static_cast<std::uint64_t>(per_sec)
              << " orders/sec  (" << n << " in " << ms << " ms)"
              << ", matched ~" << (n - resting) << ", resting " << resting << "\n";
}

inline void report_latency(const char* name, const Stats& s) {
    // Derived throughput = N / total-time-inside-add(). Biased low vs the throughput
    // benchmark by ~20-30ns of per-order timer overhead on every sample.
    double derived = s.sum > 0.0 ? (s.n / (s.sum / 1e9)) : 0.0;
    std::cout << "  " << name << "  latency ns:"
              << "  p50=" << s.p50 << "  p90=" << s.p90 << "  p99=" << s.p99
              << "  p99.9=" << s.p999 << "  max=" << s.max
              << "  (mean=" << s.mean << ", min=" << s.min << ")\n"
              << "  " << name << "  derived throughput: " << static_cast<std::uint64_t>(derived) << " orders/sec\n";
}

// SimpleOrderBook stores raw Order* and does not take ownership, so the orders
// must outlive the book — we own them in a vector and free them after.

// throughput: time the whole add() loop with exactly two timestamps.
template <class Book, class Order>
void run_throughput(const char* name, const std::vector<OrderSpec>& w) {
    Book book;
    std::vector<Order*> orders;
    orders.reserve(w.size());
    for (const auto& o : w) orders.push_back(new Order(o.is_buy, o.price, o.qty));

    auto t0 = Clock::now();
    for (auto* op : orders) book.add(op);
    auto t1 = Clock::now();

    std::size_t resting = book.bids().size() + book.asks().size();
    report_throughput(name, w.size(), ns_between(t0, t1) / 1e6, resting);

    for (auto* op : orders) delete op;
}

// latency: timestamp every add(), summarize the distribution.
template <class Book, class Order>
void run_latency(const char* name, const std::vector<OrderSpec>& w) {
    Book book;
    std::vector<Order*> orders;
    orders.reserve(w.size());
    for (const auto& o : w) orders.push_back(new Order(o.is_buy, o.price, o.qty));

    std::vector<double> lat;
    lat.reserve(orders.size());
    for (auto* op : orders) {
        auto t0 = Clock::now();
        book.add(op);
        auto t1 = Clock::now();
        lat.push_back(ns_between(t0, t1));
    }
    report_latency(name, summarize(std::move(lat)));

    for (auto* op : orders) delete op;
}

inline std::size_t parse_n(int argc, char** argv) {
    return (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 1'000'000;
}

} // namespace bench

#endif // BABO_BENCH_COMMON_H
