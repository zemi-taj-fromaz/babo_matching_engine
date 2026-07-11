// liqui_scaling.cpp — cancel throughput vs resting-book size for liquibook.
//
// Same protocol as babo_scaling: prefill N non-crossing resting bids packed into
// L price levels (untimed, incl. the LO allocations liquibook owns by pointer),
// then time cancelling all N in a deterministic shuffled order. liquibook cancels
// via find_on_market, which rescans the contended price level, so ns/cancel
// should climb with per-level depth D = N / L. Counterpart: babo_scaling.
//
//   liqui_scaling [--sizes N,N,...] [--levels L] [--reps R] [--max M]
//
#include "scaling_common.h"

#include "liqui_book_type.h"          // canonical babo_bench::LiquiBook
#include <simple/simple_order.h>
#include <simple/simple_order_book.h>

#include <chrono>
#include <cstdint>
#include <vector>

namespace lb = liquibook::book;
using LO   = liquibook::simple::SimpleOrder;
using Book = babo_bench::LiquiBook;   // canonical SimpleOrderBook<5>
using clk  = std::chrono::steady_clock;

static constexpr unsigned kBenchCore = 5;

static constexpr const char* kEngineName = "liquibook";
#ifdef LIQUI_NO_DEPTH
static constexpr bool kDepthOn = false;
#else
static constexpr bool kDepthOn = true;
#endif

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

// Build a fresh book, prefill n resting bids (off the clock, allocations
// included), then time cancelling every id in `order`. Returns cancel wall
// seconds. LO objects are freed after the timed region.
double measure(std::uint64_t n, const std::vector<std::uint32_t>& order, unsigned levels) {
    Book book; NoopListener nl; book.set_order_listener(&nl);
    std::vector<LO*> by_id(n + 1, nullptr);              // id -> LO*, off the clock

    for (std::uint64_t i = 1; i <= n; ++i) {             // prefill (untimed)
        LO* lo = new LO(/*is_buy=*/true,
                        lb::Price(bench::scaling::price_for(i, levels)),
                        lb::Quantity(10), 0, lb::oc_no_conditions);
        by_id[i] = lo;
        book.add(lo, lb::oc_no_conditions);
    }

    const auto t0 = clk::now();
    for (std::uint32_t id : order) book.cancel(by_id[id]);
    const auto t1 = clk::now();

    // Observe drained state (sink), then free the orders (after timing).
    std::uint32_t remaining = 0;
    for (LO* lo : by_id) if (lo && lo->open_qty() > 0) ++remaining;
    volatile std::uint32_t sink = remaining; (void)sink;
    for (LO* lo : by_id) delete lo;

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
    bench::log::note("experiment: cancel throughput vs resting-book size");
    bench::log::pin_and_isolate(kBenchCore);

    bench::scaling::run(kEngineName, kDepthOn, kBenchCore, opt,
        [&](std::uint64_t n, const std::vector<std::uint32_t>& order) {
            return measure(n, order, opt.levels);
        });
    return 0;
}
