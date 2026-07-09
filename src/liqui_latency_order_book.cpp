// liqui_latency_order_book.cpp — per-operation latency micro-benchmark for liquibook.
//
// Same protocol as babo_latency: replay a generator .bin with a NO-OP listener,
// time EACH message with rdtscp, report p50/p90/p99/p99.9/max in ns. Liquibook
// cancels by pointer (id->LO* map, sized off the clock); the per-NEW heap
// allocation is timed as part of that message's latency, as in production.
// Counterpart: babo_latency_order_book.cpp.
//
//   liqui_latency <workload.bin>
//
#include "bench/bench_util.h"
#include "bench/workload_reader.h"

#include <simple/simple_order.h>
#include <simple/simple_order_book.h>

#include <cstdint>
#include <cstdio>
#include <vector>

namespace lb = liquibook::book;
using LO   = liquibook::simple::SimpleOrder;
using Book = liquibook::simple::SimpleOrderBook<5>;
using liquibook::simple::os_accepted;

static constexpr unsigned kBenchCore = 2;

namespace {

struct NoopListener : lb::OrderListener<LO*> {
    void on_accept(LO* const&) override {}
    void on_fill(LO* const&, LO* const&, lb::Quantity, lb::Cost) override {}
    void on_cancel(LO* const&) override {}
    void on_replace(LO* const&, const std::int32_t&, lb::Price) override {}
    void on_reject(LO* const&, const char*) override {}
    void on_cancel_reject(LO* const&, const char*) override {}
    void on_replace_reject(LO* const&, const char*) override {}
};

inline bool resting(LO* lo) {
    return lo && lo->state() == os_accepted && lo->open_qty() > 0;
}

std::size_t max_order_id(const std::vector<bench::WMsg>& w) {
    std::size_t m = 0;
    for (const auto& x : w) if (std::size_t(x.order_id) > m) m = std::size_t(x.order_id);
    return m;
}

// Dispatch one message, recording every allocation in `allocated` for cleanup.
inline void dispatch(Book& book, std::vector<LO*>& orders,
                     std::vector<LO*>& allocated, const bench::WMsg& m) {
    const std::uint32_t id = std::uint32_t(m.order_id);
    if (m.type == 0) {
        const lb::OrderConditions c =
            m.ioc ? lb::oc_immediate_or_cancel : lb::oc_no_conditions;
        LO* lo = new LO(m.side == 0, lb::Price(m.price), lb::Quantity(m.qty), 0, c);
        allocated.push_back(lo);
        orders[id] = lo;
        book.add(lo, c);
    } else if (m.type == 1) {
        LO* lo = orders[id];
        if (resting(lo)) book.cancel(lo);
    } else {
        LO* lo = orders[id];
        if (resting(lo)) {
            book.cancel(lo);
            LO* n = new LO(m.side == 0, lb::Price(m.price), lb::Quantity(m.qty),
                           0, lb::oc_no_conditions);
            allocated.push_back(n);
            orders[id] = n;
            book.add(n, lb::oc_no_conditions);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    bench::pin_and_isolate(kBenchCore);
    const double tpn = bench::calibrate_ticks_per_ns();

    const char* path = (argc > 1) ? argv[1] : "orders_normal_s23_n1000000.bin";
    std::vector<bench::WMsg> w;
    if (!bench::load_workload(path, w)) return 1;
    std::printf("liquibook latency: %zu messages from %s\n", w.size(), path);

    const std::size_t max_id = max_order_id(w);

    {   // warmup, then free what it built
        Book warm; NoopListener nl; warm.set_order_listener(&nl);
        std::vector<LO*> orders(max_id + 1, nullptr), allocated;
        allocated.reserve(w.size());
        for (const auto& m : w) dispatch(warm, orders, allocated, m);
        for (LO* lo : allocated) delete lo;
    }

    Book book; NoopListener nl; book.set_order_listener(&nl);          // measured
    std::vector<LO*> orders(max_id + 1, nullptr), allocated;
    allocated.reserve(w.size());
    std::vector<std::uint32_t> ticks(w.size());
    for (std::size_t i = 0; i < w.size(); ++i) {
        const std::uint64_t a = bench::tsc_now();
        dispatch(book, orders, allocated, w[i]);
        const std::uint64_t b = bench::tsc_now();
        ticks[i] = std::uint32_t(b - a);
    }

    std::printf("liquibook per-message latency:\n");
    bench::report_latency(ticks, tpn);
    for (LO* lo : allocated) delete lo;
    return 0;
}
