#pragma once
// bench_util.h — shared helpers for the standalone engine micro-benchmarks:
// core pinning + isolation, an rdtscp cycle clock (x86) with ns calibration, and
// throughput / latency-percentile reporting. Header-only, engine-agnostic.

#if defined(__linux__) && !defined(_GNU_SOURCE)
#  define _GNU_SOURCE            // sched_setaffinity / CPU_SET live behind this
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX             // keep <windows.h> from clobbering std::min/max
#  endif
#  include <windows.h>
#else
#  include <sched.h>
#endif

// rdtscp lives in <intrin.h> (MSVC) / <x86intrin.h> (gcc/clang) on x86 only.
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#  if defined(_MSC_VER)
#    include <intrin.h>
#  else
#    include <x86intrin.h>
#  endif
#  define BENCH_TSC 1
#else
#  define BENCH_TSC 0
#endif

namespace bench {

// --- core pinning + isolation ----------------------------------------------
#if defined(_WIN32)
inline bool pin_to_core(unsigned core) {
    return SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << core) != 0;
}
// No isolcpus on Windows; raising priority is the programmatic isolation — this
// thread preempts other normal work the scheduler may still place on the core.
inline bool isolate_core() {
    return SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL) != 0;
}
#else
inline bool pin_to_core(unsigned core) {
    cpu_set_t set; CPU_ZERO(&set); CPU_SET(core, &set);
    return sched_setaffinity(0, sizeof(set), &set) == 0;   // 0 == calling thread
}
// SCHED_FIFO preempts all normal (SCHED_OTHER) tasks on the core. Needs
// CAP_SYS_NICE/root; isolcpus/nohz_full are boot params, this is the runtime approx.
inline bool isolate_core() {
    struct sched_param sp;
    sp.sched_priority = sched_get_priority_max(SCHED_FIFO) - 1;
    return sched_setscheduler(0, SCHED_FIFO, &sp) == 0;
}
#endif

inline void pin_and_isolate(unsigned core) {
    std::fprintf(stderr, pin_to_core(core)
        ? "pinned to core %u\n" : "warning: could not pin to core %u\n", core);
    std::fprintf(stderr, isolate_core()
        ? "raised to real-time priority (isolating the core)\n"
        : "warning: could not raise priority (need admin/root?)\n");
}

// --- cycle clock ------------------------------------------------------------
#if BENCH_TSC
inline std::uint64_t tsc_now() { unsigned aux; return __rdtscp(&aux); }
#else
inline std::uint64_t tsc_now() {
    return std::uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
}
#endif

// Cycles per nanosecond, from a short calibration spin (1.0 on the chrono
// fallback, which already counts nanoseconds). Call once, after pinning.
inline double calibrate_ticks_per_ns() {
#if BENCH_TSC
    using clk = std::chrono::steady_clock;
    const std::uint64_t c0 = tsc_now();
    const auto t0 = clk::now();
    while (std::chrono::duration<double, std::milli>(clk::now() - t0).count() < 50.0) {}
    const std::uint64_t c1 = tsc_now();
    const auto t1 = clk::now();
    const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    return double(c1 - c0) / ns;
#else
    return 1.0;
#endif
}

// --- reporting --------------------------------------------------------------
inline void report_throughput(std::size_t n, double seconds) {
    std::printf("  messages   : %zu\n", n);
    std::printf("  wall time  : %.4f s\n", seconds);
    std::printf("  throughput : %.2f M msgs/s\n", double(n) / seconds / 1e6);
}

// Sorts `ticks` in place. Values are raw cycle counts; ticks_per_ns converts them.
inline void report_latency(std::vector<std::uint32_t>& ticks, double ticks_per_ns) {
    if (ticks.empty()) return;
    std::sort(ticks.begin(), ticks.end());
    const auto ns_at = [&](double p) {
        const std::size_t i = std::size_t(p / 100.0 * double(ticks.size() - 1));
        return double(ticks[i]) / ticks_per_ns;
    };
    long double sum = 0; for (std::uint32_t t : ticks) sum += t;
    std::printf("  samples    : %zu\n", ticks.size());
    std::printf("  mean       : %8.1f ns\n", double(sum / ticks.size()) / ticks_per_ns);
    std::printf("  p50        : %8.1f ns\n", ns_at(50.0));
    std::printf("  p90        : %8.1f ns\n", ns_at(90.0));
    std::printf("  p99        : %8.1f ns\n", ns_at(99.0));
    std::printf("  p99.9      : %8.1f ns\n", ns_at(99.9));
    std::printf("  max        : %8.1f ns\n", double(ticks.back()) / ticks_per_ns);
}

}  // namespace bench
