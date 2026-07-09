// babo_latency_order_book.cpp — per-operation latency micro-benchmark for babo.
//
// Replays a generator .bin with a NO-OP listener and times EACH message with
// rdtscp, then reports p50/p90/p99/p99.9/max in nanoseconds. Per-op timestamping
// perturbs throughput, which is exactly why latency is a separate binary from
// perf. Core-pinned, with a warmup pass. Counterpart: liqui_latency_order_book.cpp.
//
//   babo_latency <workload.bin>
//
#include "bench/bench_util.h"
#include "bench/workload_reader.h"

#include <book/matching_book.h>
#include <simple/simple_order.h>

#include <cstdint>
#include <cstdio>
#include <vector>

namespace bk = babo::book;
using Order  = babo::simple::SimpleOrder;
using Book   = bk::matching_book<5>;

static constexpr unsigned kBenchCore = 2;

namespace {

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

inline void dispatch(Book& book, const bench::WMsg& m) {
    const std::uint32_t id = std::uint32_t(m.order_id);
    if (m.type == 0) {
        Order o(m.side == 0, std::uint32_t(m.price), m.qty, 0,
                m.ioc ? bk::oc_immediate_or_cancel : bk::oc_no_conditions);
        o.order_id_ = id;
        book.add(o);
    } else if (m.type == 1) {
        if (find_resting(book, id)) book.cancel(id);
    } else {
        if (find_resting(book, id)) {
            book.cancel(id);
            Order n(m.side == 0, std::uint32_t(m.price), m.qty);
            n.order_id_ = id;
            book.add(n);
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
    std::printf("babo latency: %zu messages from %s\n", w.size(), path);

    { Book warm; NoopListener nl; warm.set_order_listener(&nl);        // warmup
      for (const auto& m : w) dispatch(warm, m); }

    Book book; NoopListener nl; book.set_order_listener(&nl);          // measured
    std::vector<std::uint32_t> ticks(w.size());
    for (std::size_t i = 0; i < w.size(); ++i) {
        const std::uint64_t a = bench::tsc_now();
        dispatch(book, w[i]);
        const std::uint64_t b = bench::tsc_now();
        ticks[i] = std::uint32_t(b - a);
    }

    std::printf("babo per-message latency:\n");
    bench::report_latency(ticks, tpn);
    return 0;
}
