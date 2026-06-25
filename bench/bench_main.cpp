// --- bench -------------------------------------------------------------------
// Drives liquibook_ref and babo_book through one identical, deterministic order
// stream. Two modes:
//   throughput -- 2 timestamps total, clean overhead-free orders/sec.
//   latency    -- timestamp every add(), report the per-order distribution
//                 (p50/p90/p99/p99.9/max) plus a *derived* throughput.
//
// Why both: in a single-threaded back-to-back loop, throughput == 1 / mean
// latency, so the distribution already contains throughput. But per-order
// timestamping adds ~20-30ns of timer overhead to EVERY measurement, so the
// derived number is biased low. The throughput mode pays that timer cost only
// twice, so it's the honest orders/sec. If the two disagree, the gap is your
// measurement overhead.
//
// Usage: babo_bench [N orders] [throughput|latency|both]   (default: 1e6 both)
//
// The babo numbers are placeholder until libs/babo_book is implemented.
#include <simple/simple_order_book.h>   // liquibook_ref
#include "babo/order_book.hpp"          // babo_book

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

double ns_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::nano>(b - a).count();
}

struct OrderSpec {
    bool          is_buy;
    std::uint32_t price;
    std::uint32_t qty;
};

// Deterministic workload (fixed seed) so both engines see the exact same input
// and runs are reproducible. Prices cluster around a midpoint so a healthy
// fraction of orders cross and actually exercise the matching path.
std::vector<OrderSpec> make_workload(std::size_t n) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<std::uint32_t> price(1880, 1893);
    std::uniform_int_distribution<std::uint32_t> lots(1, 10);

    std::vector<OrderSpec> w;
    w.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        bool is_buy = (i % 2) == 0;
        w.push_back({is_buy, price(rng), lots(rng) * 100});
    }
    return w;
}

// --- throughput: time the whole add() loop with exactly two timestamps -------

void report_throughput(const char* name, std::size_t n, double ms,
                       std::size_t resting) {
    double per_sec = ms > 0.0 ? (n / (ms / 1000.0)) : 0.0;
    std::cout << "  " << name << "  throughput: " << static_cast<std::uint64_t>(per_sec)
              << " orders/sec  (" << n << " in " << ms << " ms)"
              << ", matched ~" << (n - resting) << ", resting " << resting << "\n";
}

void throughput_liquibook(const std::vector<OrderSpec>& w) {
    // SimpleOrderBook owns nothing; orders must outlive the book.
    liquibook::simple::SimpleOrderBook<5> book;
    std::vector<liquibook::simple::SimpleOrder*> orders;
    orders.reserve(w.size());
    for (const auto& o : w)
        orders.push_back(new liquibook::simple::SimpleOrder(o.is_buy, o.price, o.qty));

    auto t0 = Clock::now();
    for (auto* op : orders) book.add(op);
    auto t1 = Clock::now();

    std::size_t resting = book.bids().size() + book.asks().size();
    report_throughput("liquibook_ref", w.size(), ns_between(t0, t1) / 1e6, resting);

    for (auto* op : orders) delete op;
}

void throughput_babo(const std::vector<OrderSpec>& w) {
    babo::OrderBook book;
    auto t0 = Clock::now();
    babo::OrderId id = 0;
    for (const auto& o : w) book.add(id++, o.is_buy, o.price, o.qty);
    auto t1 = Clock::now();

    report_throughput("babo_book    ", w.size(), ns_between(t0, t1) / 1e6,
                      book.resting_count());
}

// --- latency: timestamp every add(), summarize the distribution --------------

struct Stats {
    std::size_t n = 0;
    double min = 0, p50 = 0, p90 = 0, p99 = 0, p999 = 0, max = 0, mean = 0, sum = 0;
};

// Takes the latency samples by value: we sort them in place.
Stats summarize(std::vector<double> lat) {
    std::sort(lat.begin(), lat.end());
    Stats s;
    s.n = lat.size();
    if (s.n == 0) return s;

    auto pct = [&](double p) {
        std::size_t idx = static_cast<std::size_t>(p * (lat.size() - 1));
        return lat[idx];
    };
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

void report_latency(const char* name, const Stats& s) {
    // Derived throughput = N / total-time-spent-inside-add(). Biased low vs the
    // throughput mode by the per-order timer overhead -- see header comment.
    double derived = s.sum > 0.0 ? (s.n / (s.sum / 1e9)) : 0.0;
    std::cout << "  " << name << "  latency ns:"
              << "  p50=" << s.p50
              << "  p90=" << s.p90
              << "  p99=" << s.p99
              << "  p99.9=" << s.p999
              << "  max=" << s.max
              << "  (mean=" << s.mean << ", min=" << s.min << ")\n"
              << "  " << name << "  derived throughput: "
              << static_cast<std::uint64_t>(derived) << " orders/sec\n";
}

void latency_liquibook(const std::vector<OrderSpec>& w) {
    liquibook::simple::SimpleOrderBook<5> book;
    std::vector<liquibook::simple::SimpleOrder*> orders;
    orders.reserve(w.size());
    for (const auto& o : w)
        orders.push_back(new liquibook::simple::SimpleOrder(o.is_buy, o.price, o.qty));

    std::vector<double> lat;
    lat.reserve(orders.size());
    for (auto* op : orders) {
        auto t0 = Clock::now();
        book.add(op);
        auto t1 = Clock::now();
        lat.push_back(ns_between(t0, t1));
    }
    report_latency("liquibook_ref", summarize(std::move(lat)));

    for (auto* op : orders) delete op;
}

void latency_babo(const std::vector<OrderSpec>& w) {
    babo::OrderBook book;
    std::vector<double> lat;
    lat.reserve(w.size());
    babo::OrderId id = 0;
    for (const auto& o : w) {
        auto t0 = Clock::now();
        book.add(id++, o.is_buy, o.price, o.qty);
        auto t1 = Clock::now();
        lat.push_back(ns_between(t0, t1));
    }
    report_latency("babo_book    ", summarize(std::move(lat)));
}

} // namespace

int main(int argc, char** argv) {
    std::size_t n  = (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 1'000'000;
    std::string md = (argc > 2) ? argv[2] : "both";

    bool do_thru = (md == "both" || md == "throughput");
    bool do_lat  = (md == "both" || md == "latency");
    if (!do_thru && !do_lat) {
        std::cerr << "mode must be: throughput | latency | both\n";
        return 1;
    }

    std::cout << "matching-engine benchmark (" << n << " orders, mode=" << md << ")\n";
    auto workload = make_workload(n);

    if (do_thru) {
        std::cout << "[throughput]\n";
        throughput_liquibook(workload);
        throughput_babo(workload);
    }
    if (do_lat) {
        std::cout << "[latency]\n";
        latency_liquibook(workload);
        latency_babo(workload);
    }
    return 0;
}
