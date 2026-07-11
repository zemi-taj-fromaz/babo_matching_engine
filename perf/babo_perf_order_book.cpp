// babo_perf_order_book.cpp — throughput micro-benchmark for babo's matching_book.
//
// Replays a generator .bin workload through the engine with a NO-OP listener (no
// report emission), so the measured time is the matching core alone — the tool
// for profiling the data structure / template params under perf/uProf. Core-
// pinned, with a warmup pass. Counterpart: liqui_perf_order_book.cpp.
//
//   babo_perf [workload.bin] [--reps N]
//   babo_perf [--scenario static|normal|swing25|flash_crash_40|flash_crash_60|all] [--reps N]
//
#include "bench_util.h"
#include "workload.hpp"
#include "workload_reader.h"
#include "bench_log.h"

#include <cstdlib>
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

static constexpr unsigned kBenchCore = 5;      // change to a free physical core
static constexpr int kMeasuredReps = 100;       // dilutes startup/load/print noise in perf stat

// Canonical build: pull-based depth ON. CMake does not build a duplicate
// no-depth perf matrix because it measured no meaningful difference after the
// pull-based optimization.
static constexpr const char* kEngineName = "babobook";
#ifdef BABO_NO_DEPTH
static constexpr bool kDepthOn = false;
#else
static constexpr bool kDepthOn = true;
#endif

namespace {

struct Options {
    const char* workload_path = nullptr;
    const char* scenario = "normal";
    int reps = kMeasuredReps;
    long count = 1'000'000;      // generator NEW-order count (message-scale knob)
};

void print_usage(const char* exe) {
    std::fprintf(stderr,
        "usage:\n"
        "  %s [workload.bin] [--reps N]\n"
        "  %s [--scenario ...|all] [--reps N] [--count NEW_ORDERS]\n",
        exe, exe);
}

bool parse_bounded_long(const char* text, long lo, long hi, long& out) {
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (!text[0] || *end != '\0' || value < lo || value > hi) return false;
    out = value;
    return true;
}

bool parse_positive_int(const char* text, int& out) {
    long v; if (!parse_bounded_long(text, 1, 1'000'000, v)) return false;
    out = static_cast<int>(v);
    return true;
}

bool parse_options(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--reps") {
            if (++i >= argc || !parse_positive_int(argv[i], options.reps)) return false;
        } else if (arg == "--count") {
            if (++i >= argc || !parse_bounded_long(argv[i], 2, 50'000'000, options.count)) return false;
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

// One full replay on a fresh book. Writes the resting-count sink.
void replay_once(const std::vector<bench::WMsg>& w, std::uint32_t& sink) {
    Book book; NoopListener nl; book.set_order_listener(&nl);
    for (const auto& m : w) dispatch(book, m);
    sink = resting_count(book);
}

// Times each rep separately (into `per_rep`) and returns the aggregate wall.
double replay_repeated(const std::vector<bench::WMsg>& w, int reps,
                       std::uint32_t& sink, std::vector<double>& per_rep) {
    per_rep.resize(static_cast<std::size_t>(reps));
    const auto t0 = clk::now();
    for (int rep = 0; rep < reps; ++rep) {
        const auto a = clk::now();
        replay_once(w, sink);
        const auto b = clk::now();
        per_rep[rep] = std::chrono::duration<double>(b - a).count();
    }
    const auto t1 = clk::now();
    return std::chrono::duration<double>(t1 - t0).count();
}

void run_workload(const char* label, const std::vector<bench::WMsg>& w, int reps,
                  std::uint64_t num_new) {
    std::uint32_t sink = 0;
    replay_once(w, sink);                              // warmup
    std::vector<double> per_rep;
    const double wall = replay_repeated(w, reps, sink, per_rep);   // measured hot loop
    std::sort(per_rep.begin(), per_rep.end());
    const double best   = per_rep.front();
    const double median = per_rep[per_rep.size() / 2];
    bench::log::result(label, w.size(), reps, best, median, wall, sink);
    bench::log::perf_row(kEngineName, label, num_new, w.size(), reps, best, median, wall, sink);
}

}  // namespace

int main(int argc, char** argv) {
    bench::log::init();

    Options options;
    if (!parse_options(argc, argv, options)) {
        print_usage(argv[0]);
        return 2;
    }

    bench::log::banner(kEngineName, kDepthOn, kBenchCore, options.reps);
    bench::log::note("pin_node capacity: " + std::to_string(babo::kNodeCapacity));
    bench::log::pin_and_isolate(kBenchCore);

    if (options.workload_path) {
        const char* path = options.workload_path;
        std::vector<bench::WMsg> w;
        if (!bench::load_workload(path, w)) return 1;
        run_workload(path, w, options.reps, 0);
        return 0;
    }

    const auto run_scenario = [&](const bench::Scenario& scenario) {
        bench::WorkloadParams params;
        params.volatility = scenario.volatility;
        params.target_swing = scenario.target_swing;
        params.num_new = static_cast<std::uint32_t>(options.count);
        run_workload(scenario.name,
                     to_wire_workload(bench::WorkloadGen(params).generate()),
                     options.reps, static_cast<std::uint64_t>(options.count));
    };

    if (std::string_view(options.scenario) == "all") {
        for (const bench::Scenario& scenario : bench::kScenarios) run_scenario(scenario);
    } else if (const bench::Scenario* scenario = find_scenario(options.scenario)) {
        run_scenario(*scenario);
    } else {
        bench::log::error(std::string("unknown scenario: ") + options.scenario);
        print_usage(argv[0]);
        return 2;
    }
    return 0;
}
