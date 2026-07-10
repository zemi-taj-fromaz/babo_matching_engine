// liqui_perf_order_book.cpp — throughput micro-benchmark for liquibook.
//
// Same protocol as babo_perf: replay a generator .bin with a NO-OP listener and
// time the matching core alone. Liquibook cancels by POINTER, so this keeps an
// id->LO* map (pre-sized off the clock). Order allocation stays inside the timed
// loop, as it does in production — that heap cost is part of what the comparison
// measures. Counterpart: babo_perf_order_book.cpp.
//
//   liqui_perf [workload.bin] [--reps N]
//   liqui_perf [--scenario static|normal|swing25|flash_crash_40|flash_crash_60|all] [--reps N]
//
#include "bench_util.h"
#include "workload.hpp"
#include "workload_reader.h"

#include <cstdlib>
#include "liqui_book_type.h"       // babo_bench::LiquiBook (depth ON/OFF via -DLIQUI_NO_DEPTH)
#include <simple/simple_order.h>
#include <simple/simple_order_book.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace lb = liquibook::book;
using LO   = liquibook::simple::SimpleOrder;
using Book = babo_bench::LiquiBook;   // SimpleOrderBook<5> (depth ON) or NoDepthBook (-DLIQUI_NO_DEPTH)
using liquibook::simple::os_accepted;
using clk  = std::chrono::steady_clock;

static constexpr unsigned kBenchCore = 5;
static constexpr int kMeasuredReps = 100;

namespace {

struct Options {
    const char* workload_path = nullptr;
    const char* scenario = "normal";
    int reps = kMeasuredReps;
};

void print_usage(const char* exe) {
    std::fprintf(stderr,
        "usage:\n"
        "  %s [workload.bin] [--reps N]\n"
        "  %s [--scenario static|normal|swing25|flash_crash_40|flash_crash_60|all] [--reps N]\n",
        exe, exe);
}

bool parse_positive_int(const char* text, int& out) {
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (!text[0] || *end != '\0' || value <= 0 || value > 1'000'000) return false;
    out = static_cast<int>(value);
    return true;
}

bool parse_options(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--reps") {
            if (++i >= argc || !parse_positive_int(argv[i], options.reps)) return false;
        } else if (arg == "--scenario") {
            if (++i >= argc) return false;
            options.scenario = argv[i];
        } else if (!arg.empty() && arg[0] == '-') {
            return false;
        } else if (!options.workload_path) {
            options.workload_path = argv[i];
        } else {
            return false;
        }
    }
    return true;
}

const bench::Scenario* find_scenario(std::string_view name) {
    for (const auto& scenario : bench::kScenarios) {
        if (name == scenario.name) return &scenario;
    }
    return nullptr;
}

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

std::vector<bench::WMsg> to_wire_workload(const std::vector<bench::Msg>& generated) {
    std::vector<bench::WMsg> out;
    out.reserve(generated.size());
    std::uint64_t seq = 1;
    for (const auto& m : generated) {
        const std::uint8_t type =
            m.op == bench::Op::Add ? 0 : (m.op == bench::Op::Cancel ? 1 : 2);
        out.push_back(bench::WMsg{
            type,
            static_cast<std::uint8_t>(m.is_buy ? 0 : 1),
            static_cast<std::uint8_t>(m.ioc ? 1 : 0),
            m.qty,
            seq++,
            m.id,
            static_cast<std::int64_t>(m.price)
        });
    }
    return out;
}

// One full replay on a fresh book. `orders` (id->LO*) is sized off the clock; the
// LO allocations are part of the replay because liquibook owns orders by pointer.
void replay_once(const std::vector<bench::WMsg>& w, std::size_t max_id, std::uint32_t& sink) {
    Book book; NoopListener nl; book.set_order_listener(&nl);
    std::vector<LO*> orders(max_id + 1, nullptr);      // off the clock
    std::vector<LO*> allocated;
    allocated.reserve(w.size());

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

    std::uint32_t n = 0;
    for (LO* lo : allocated) if (resting(lo)) ++n;      // sink
    sink = n;
    for (LO* lo : allocated) delete lo;
}

double replay_repeated(const std::vector<bench::WMsg>& w, std::size_t max_id, int reps, std::uint32_t& sink) {
    const auto t0 = clk::now();
    for (int rep = 0; rep < reps; ++rep) replay_once(w, max_id, sink);
    const auto t1 = clk::now();
    return std::chrono::duration<double>(t1 - t0).count();
}

void run_workload(const char* label, const std::vector<bench::WMsg>& w, int reps) {
    std::printf("liquibook perf: %s: %zu messages x %d reps\n", label, w.size(), reps);

    const std::size_t max_id = max_order_id(w);
    std::uint32_t sink = 0;

    replay_once(w, max_id, sink);                             // warmup
    const double sec = replay_repeated(w, max_id, reps, sink); // measured hot loop

    bench::report_throughput(w.size() * static_cast<std::size_t>(reps), sec);
    std::printf("  resting    : %u  (sink)\n", sink);
}

}  // namespace

int main(int argc, char** argv) {
    bench::pin_and_isolate(kBenchCore);

    Options options;
    if (!parse_options(argc, argv, options)) {
        print_usage(argv[0]);
        return 2;
    }

    if (options.workload_path) {
        const char* path = options.workload_path;
        std::vector<bench::WMsg> w;
        if (!bench::load_workload(path, w)) return 1;
        run_workload(path, w, options.reps);
        return 0;
    }

    const auto run_scenario = [&](const bench::Scenario& scenario) {
        bench::WorkloadParams params;
        params.volatility = scenario.volatility;
        params.target_swing = scenario.target_swing;
        run_workload(scenario.name, to_wire_workload(bench::WorkloadGen(params).generate()), options.reps);
    };

    if (std::string_view(options.scenario) == "all") {
        for (const bench::Scenario& scenario : bench::kScenarios) run_scenario(scenario);
    } else if (const bench::Scenario* scenario = find_scenario(options.scenario)) {
        run_scenario(*scenario);
    } else {
        std::fprintf(stderr, "unknown scenario: %s\n", options.scenario);
        print_usage(argv[0]);
        return 2;
    }
    return 0;
}
