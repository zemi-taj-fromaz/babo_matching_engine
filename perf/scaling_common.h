#pragma once
// scaling_common.h — shared harness for the "cancel throughput vs resting-book
// size" micro-benchmark (the O(1)-cancel money figure).
//
// The experiment isolates *cancel* cost as the resting book grows. Each engine
// prefills N non-crossing resting orders packed into a fixed number of price
// levels L, so the depth per contended price is D = N / L and grows with N.
// Then it times cancelling ALL N orders in a fixed, deterministic *shuffled*
// order (so a per-level scan hits an average position, not the front). Order
// allocation / prefill is OFF the clock — only the cancel loop is timed.
//
//   babo   cancel: O(1)             (id -> slot via a hash index)  -> flat line
//   liqui  cancel: O(log N + D)     (find_on_market rescans the level) -> rises
//
// Engine-specific code is a single `measure` callable supplied by each binary;
// everything else (arg parsing, the size sweep, the portable shuffle, best-of-N
// reps, and both the pretty and machine-parseable output) lives here.
//
// Shared flags:  --sizes a,b,c   --levels L   --reps N   --max M
#include "bench_log.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace bench::scaling {

struct Options {
    std::vector<std::uint64_t> sizes = {1'000, 2'000, 5'000, 10'000,
                                        20'000, 50'000, 100'000, 200'000};
    unsigned levels = 64;   // price levels the N orders are packed into
    int      reps   = 3;    // best-of-N per size (min wall = least interference)
    std::uint32_t seed = 0x9E3779B9u;   // fixed: identical cancel order everywhere
};

// Prices are laid out so order id `i` (1-based) rests at level (i-1) % levels,
// price = kBasePrice - level. All orders are bids and there are no asks, so
// nothing ever crosses and all N rest. Base is high enough to stay positive.
inline constexpr std::int64_t kBasePrice = 4'000'000;
inline std::int64_t price_for(std::uint64_t id_1based, unsigned levels) {
    return kBasePrice - static_cast<std::int64_t>((id_1based - 1) % levels);
}

// Portable Fisher-Yates over ids [1..N]. mt19937_64's raw output is identical
// across libstdc++/libc++/MSVC for a given seed; modulo bias is irrelevant for a
// benchmark shuffle. Same seed => byte-identical cancel order on every machine.
inline std::vector<std::uint32_t> shuffled_ids(std::uint64_t n, std::uint32_t seed) {
    std::vector<std::uint32_t> ids(n);
    for (std::uint64_t i = 0; i < n; ++i) ids[i] = static_cast<std::uint32_t>(i + 1);
    std::mt19937_64 rng(seed);
    for (std::uint64_t i = n; i > 1; --i) {
        const std::uint64_t j = rng() % i;        // [0, i-1]
        std::swap(ids[i - 1], ids[j]);
    }
    return ids;
}

inline bool parse_u64_list(const char* text, std::vector<std::uint64_t>& out) {
    out.clear();
    const std::string s(text);
    std::size_t i = 0;
    while (i < s.size()) {
        std::size_t j = s.find(',', i);
        if (j == std::string::npos) j = s.size();
        if (j == i) return false;
        char* end = nullptr;
        const unsigned long long v = std::strtoull(s.c_str() + i, &end, 10);
        if (end != s.c_str() + j || v < 2) return false;   // need >=2 to shuffle
        out.push_back(static_cast<std::uint64_t>(v));
        i = j + 1;
    }
    return !out.empty();
}

inline bool parse_positive_int(const char* text, long lo, long hi, long& out) {
    char* end = nullptr;
    const long v = std::strtol(text, &end, 10);
    if (!text[0] || *end != '\0' || v < lo || v > hi) return false;
    out = v;
    return true;
}

inline void print_usage(const char* exe) {
    std::fprintf(stderr,
        "usage: %s [--sizes N,N,...] [--levels L] [--reps R] [--max M]\n"
        "  --sizes   resting-book sizes to sweep (default 1k..200k)\n"
        "  --levels  price levels the orders pack into (default 64)\n"
        "  --reps    best-of-N reps per size (default 3)\n"
        "  --max     append one extra size M to the sweep (e.g. 1000000)\n",
        exe);
}

inline bool parse_options(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--sizes") {
            if (++i >= argc || !parse_u64_list(argv[i], o.sizes)) return false;
        } else if (a == "--levels") {
            long v; if (++i >= argc || !parse_positive_int(argv[i], 1, 1'000'000, v)) return false;
            o.levels = static_cast<unsigned>(v);
        } else if (a == "--reps") {
            long v; if (++i >= argc || !parse_positive_int(argv[i], 1, 10'000, v)) return false;
            o.reps = static_cast<int>(v);
        } else if (a == "--max") {
            long v; if (++i >= argc || !parse_positive_int(argv[i], 2, 2'000'000'000, v)) return false;
            o.sizes.push_back(static_cast<std::uint64_t>(v));
        } else {
            return false;
        }
    }
    return true;
}

// `measure(n, cancel_order)` builds a fresh book, prefills n resting orders
// (untimed), times cancelling every id in `cancel_order`, and returns the cancel
// wall time in seconds. It must read the drained book state so the loop is not
// optimized away.
template <class Measure>
void run(const char* engine, bool depth_on, unsigned core,
         const Options& o, Measure&& measure) {
    for (std::uint64_t n : o.sizes) {
        const auto order = shuffled_ids(n, o.seed);
        measure(n, order);                              // warmup (discarded)
        double best = 1e300;
        for (int r = 0; r < o.reps; ++r) {
            const double sec = measure(n, order);
            if (sec < best) best = sec;
        }
        const double mps = best > 0.0 ? double(n) / best / 1e6 : 0.0;
        const double ns  = best > 0.0 ? best / double(n) * 1e9 : 0.0;
        const std::uint64_t depth = n / o.levels;

        std::printf("\n  %ssize %s%s   %sdepth ~%s/lvl (%u lvls)%s   "
                    "%scancel %.2f M/s%s   %.1f ns/cancel   %s(best of %d)%s\n",
                    log::ansi::bold, log::commas(n).c_str(), log::ansi::reset,
                    log::ansi::dim, log::commas(depth).c_str(), o.levels, log::ansi::reset,
                    log::ansi::green, mps, log::ansi::reset,
                    ns, log::ansi::dim, o.reps, log::ansi::reset);
        // Machine-parseable line for the runner (stable key=value schema).
        std::printf("SCALEROW engine=%s size=%llu levels=%u depth=%llu "
                    "cancels=%llu seconds=%.6f mps=%.4f ns_per_cancel=%.3f\n",
                    engine,
                    static_cast<unsigned long long>(n), o.levels,
                    static_cast<unsigned long long>(depth),
                    static_cast<unsigned long long>(n), best, mps, ns);
        std::fflush(stdout);
    }
    (void)depth_on; (void)core;
}

}  // namespace bench::scaling
