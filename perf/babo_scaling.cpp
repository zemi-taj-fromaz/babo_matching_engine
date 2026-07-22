// babo_scaling.cpp — cancel throughput vs resting-book size for babo.
//
// Prefills N non-crossing resting bids packed into L price levels (untimed),
// then times cancelling all N by id in a deterministic shuffled order. babo's
// cancel is O(1) (id -> slot via a hash index), so the ns/cancel line should
// stay flat as N (and thus per-level depth) grows. Counterpart: liqui_scaling.
//
//   babo_scaling [--sizes N,N,...] [--levels L] [--reps R] [--max M]
//
#include "scaling_common.h"

#include <book/matching_book.h>
#include <book/simple_order.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace bk = babo::book;
using Order  = babo::simple::SimpleOrder;
using Book   = bk::matching_book<5>;      // canonical depth (matches the adapter)
using clk    = std::chrono::steady_clock;

static constexpr unsigned kBenchCore = 5;

static constexpr const char* kEngineName = "babobook";
#ifdef BABO_NO_DEPTH
static constexpr bool kDepthOn = false;
#else
static constexpr bool kDepthOn = true;
#endif

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

// Build a fresh book, prefill n resting bids (off the clock), then time the
// cancel of every id in `order`. Returns cancel wall seconds.
double measure(std::uint64_t n, const std::vector<std::uint32_t>& order, unsigned levels) {
    Book book; NoopListener nl; book.set_order_listener(&nl);

    for (std::uint64_t i = 1; i <= n; ++i) {                       // prefill (untimed)
        Order o(/*is_buy=*/true,
                static_cast<std::uint32_t>(bench::scaling::price_for(i, levels)),
                /*qty=*/10, 0, bk::oc_no_conditions);
        o.order_id_ = static_cast<std::uint32_t>(i);
        book.add(o);
    }

    const auto t0 = clk::now();
    for (std::uint32_t id : order) book.cancel(id);
    const auto t1 = clk::now();

    // Read the drained book so the cancel loop can't be optimized away.
    std::uint32_t remaining = 0;
    for (auto it = book.bids().orders_begin(); it != book.bids().orders_end(); ++it) ++remaining;
    volatile std::uint32_t sink = remaining; (void)sink;

    return std::chrono::duration<double>(t1 - t0).count();
}

}  // namespace

int main(int argc, char** argv) {
    bench::log::init();

    bench::scaling::Options opt;
    if (!bench::scaling::parse_options(argc, argv, opt)) {
        bench::scaling::print_usage(argv[0]);
        return 2;
    }

    bench::log::banner(kEngineName, kDepthOn, kBenchCore, opt.reps);
    bench::log::note("pin_node capacity: " + std::to_string(babo::kNodeCapacity));
    bench::log::note("experiment: cancel throughput vs resting-book size");
    bench::log::pin_and_isolate(kBenchCore);

    bench::scaling::run(kEngineName, kDepthOn, kBenchCore, opt,
        [&](std::uint64_t n, const std::vector<std::uint32_t>& order) {
            return measure(n, order, opt.levels);
        });
    return 0;
}
