// babo_perf_order_book.cpp — throughput micro-benchmark for babo's matching_book.
//
// Replays a generator .bin workload through the engine with a NO-OP listener (no
// report emission), so the measured time is the matching core alone — the tool
// for profiling the data structure / template params under perf/uProf. Core-
// pinned, with a warmup pass. Counterpart: liqui_perf_order_book.cpp.
//
//   babo_perf <workload.bin>
//
#include "bench/bench_util.h"
#include "bench/workload_reader.h"

#include <book/matching_book.h>
#include <simple/simple_order.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace bk = babo::book;
using Order  = babo::simple::SimpleOrder;
using Book   = bk::matching_book<5>;           // canonical depth (matches the adapter)
using clk    = std::chrono::steady_clock;

static constexpr unsigned kBenchCore = 2;      // change to a free physical core

namespace {

// Matching runs; reports don't. Isolates the data structure from report cost.
struct NoopListener : bk::OrderListener<std::uint32_t> {
    void on_fill(const std::uint32_t&, const std::uint32_t&, std::uint32_t, std::uint32_t) override {}
    void on_accept(const std::uint32_t&) override {}
    void on_reject(const std::uint32_t&, const char*) override {}
    void on_cancel(const std::uint32_t&) override {}
    void on_cancel_reject(const std::uint32_t&, const char*) override {}
    void on_replace(const std::uint32_t&, const std::int32_t&, std::uint32_t) override {}
    void on_replace_reject(const std::uint32_t&, const char*) override {}
};

inline Order* find_resting(Book& b, std::uint32_t id) {
    if (Order* o = b.bids().find_order(id)) return o;
    if (Order* o = b.asks().find_order(id)) return o;
    return nullptr;
}

// Dispatch one message. babo is id-based, so cancel/modify need no id->pointer map;
// the guard matches the adapter (a duplicate/stale cancel of a gone order is a no-op).
inline void dispatch(Book& book, const bench::WMsg& m) {
    const std::uint32_t id = std::uint32_t(m.order_id);
    if (m.type == 0) {                                   // NEW
        Order o(m.side == 0, std::uint32_t(m.price), m.qty, 0,
                m.ioc ? bk::oc_immediate_or_cancel : bk::oc_no_conditions);
        o.order_id_ = id;
        book.add(o);                                     // by value: babo copies into its pool
    } else if (m.type == 1) {                            // CANCEL
        if (find_resting(book, id)) book.cancel(id);
    } else {                                             // MODIFY = cancel + reinsert
        if (find_resting(book, id)) {
            book.cancel(id);
            Order n(m.side == 0, std::uint32_t(m.price), m.qty);
            n.order_id_ = id;
            book.add(n);
        }
    }
}

// Count still-resting orders — a sink so the optimizer can't drop the replay.
std::uint32_t resting_count(Book& book) {
    std::uint32_t n = 0;
    for (auto it = book.bids().orders_begin(); it != book.bids().orders_end(); ++it) ++n;
    for (auto it = book.asks().orders_begin(); it != book.asks().orders_end(); ++it) ++n;
    return n;
}

// One full replay on a fresh book. Returns seconds; writes the resting-count sink.
double replay_timed(const std::vector<bench::WMsg>& w, std::uint32_t& sink) {
    Book book; NoopListener nl; book.set_order_listener(&nl);
    const auto t0 = clk::now();
    for (const auto& m : w) dispatch(book, m);
    const auto t1 = clk::now();
    sink = resting_count(book);
    return std::chrono::duration<double>(t1 - t0).count();
}

}  // namespace

int main(int argc, char** argv) {
    bench::pin_and_isolate(kBenchCore);

    const char* path = (argc > 1) ? argv[1] : "orders_normal_s23_n1000000.bin";
    std::vector<bench::WMsg> w;
    if (!bench::load_workload(path, w)) return 1;
    std::printf("babo perf: %zu messages from %s\n", w.size(), path);

    std::uint32_t sink = 0;
    replay_timed(w, sink);                      // warmup (warms code / branch predictor)
    const double sec = replay_timed(w, sink);   // measured

    bench::report_throughput(w.size(), sec);
    std::printf("  resting    : %u  (sink)\n", sink);
    return 0;
}
