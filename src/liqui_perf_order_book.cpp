// liqui_perf_order_book.cpp — throughput micro-benchmark for liquibook.
//
// Same protocol as babo_perf: replay a generator .bin with a NO-OP listener and
// time the matching core alone. Liquibook cancels by POINTER, so this keeps an
// id->LO* map (pre-sized off the clock). Order allocation stays inside the timed
// loop, as it does in production — that heap cost is part of what the comparison
// measures. Counterpart: babo_perf_order_book.cpp.
//
//   liqui_perf <workload.bin>
//
#include "bench/bench_util.h"
#include "bench/workload_reader.h"

#include <simple/simple_order.h>
#include <simple/simple_order_book.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace lb = liquibook::book;
using LO   = liquibook::simple::SimpleOrder;
using Book = liquibook::simple::SimpleOrderBook<5>;
using liquibook::simple::os_accepted;
using clk  = std::chrono::steady_clock;

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

// One full replay on a fresh book. `orders` (id->LO*) is sized off the clock; the
// LO allocations are on the clock (liquibook needs heap order nodes). Every LO is
// recorded in `allocated` for cleanup after timing.
double replay_timed(const std::vector<bench::WMsg>& w, std::size_t max_id,
                    std::vector<LO*>& allocated, std::uint32_t& sink) {
    Book book; NoopListener nl; book.set_order_listener(&nl);
    std::vector<LO*> orders(max_id + 1, nullptr);      // off the clock

    const auto t0 = clk::now();
    for (const auto& m : w) {
        const std::uint32_t id = std::uint32_t(m.order_id);
        if (m.type == 0) {                                          // NEW
            const lb::OrderConditions c =
                m.ioc ? lb::oc_immediate_or_cancel : lb::oc_no_conditions;
            LO* lo = new LO(m.side == 0, lb::Price(m.price), lb::Quantity(m.qty), 0, c);
            allocated.push_back(lo);
            orders[id] = lo;
            book.add(lo, c);
        } else if (m.type == 1) {                                  // CANCEL
            LO* lo = orders[id];
            if (resting(lo)) book.cancel(lo);
        } else {                                                   // MODIFY = cancel + reinsert
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
    const auto t1 = clk::now();

    std::uint32_t n = 0;
    for (LO* lo : allocated) if (resting(lo)) ++n;      // sink
    sink = n;
    return std::chrono::duration<double>(t1 - t0).count();
}

}  // namespace

int main(int argc, char** argv) {
    bench::pin_and_isolate(kBenchCore);

    const char* path = (argc > 1) ? argv[1] : "orders_normal_s23_n1000000.bin";
    std::vector<bench::WMsg> w;
    if (!bench::load_workload(path, w)) return 1;
    std::printf("liquibook perf: %zu messages from %s\n", w.size(), path);

    const std::size_t max_id = max_order_id(w);
    std::uint32_t sink = 0;

    {   // warmup (warms code / branch predictor), then free the orders it built
        std::vector<LO*> warm; warm.reserve(w.size());
        replay_timed(w, max_id, warm, sink);
        for (LO* lo : warm) delete lo;
    }

    std::vector<LO*> allocated; allocated.reserve(w.size());
    const double sec = replay_timed(w, max_id, allocated, sink);   // measured

    bench::report_throughput(w.size(), sec);
    std::printf("  resting    : %u  (sink)\n", sink);
    for (LO* lo : allocated) delete lo;
    return 0;
}
