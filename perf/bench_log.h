#pragma once
// bench_log.h — clean, colored console output for the standalone perf binaries.
// Self-contained (NO dependency): ANSI escape codes, with the Windows console's
// virtual-terminal mode enabled at init() so colors render there too. ASCII-only
// glyphs so the layout is identical on every terminal. Header-only.

#include "bench_util.h"      // bench::pin_to_core / bench::isolate_core (+ <windows.h> on Win32)

#include <cstdint>
#include <cstdio>
#include <string>

#if defined(_WIN32) && !defined(ENABLE_VIRTUAL_TERMINAL_PROCESSING)
#  define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

namespace bench::log {

namespace ansi {
    inline constexpr const char* reset = "\x1b[0m";
    inline constexpr const char* bold  = "\x1b[1m";
    inline constexpr const char* dim   = "\x1b[2m";
    inline constexpr const char* cyan  = "\x1b[36m";
    inline constexpr const char* green = "\x1b[1;32m";   // bold green (the headline number)
    inline constexpr const char* yellow= "\x1b[33m";
    inline constexpr const char* red   = "\x1b[31m";
}

// Enable ANSI color on the Windows console (Win10+); no-op elsewhere.
inline void init() {
#if defined(_WIN32)
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

// 1234567 -> "1,234,567"
inline std::string commas(std::uint64_t v) {
    std::string s = std::to_string(v);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(i, ",");
    return s;
}

// Header block: engine name, depth config, pinned core, rep count.
inline void banner(const char* engine, bool depth_on, unsigned core, int reps) {
    std::printf("\n%s%s==============================================================%s\n",
                ansi::bold, ansi::cyan, ansi::reset);
    std::printf("%s%s  %s   |   depth %s%s\n",
                ansi::bold, ansi::cyan, engine, depth_on ? "ON" : "OFF", ansi::reset);
    std::printf("%s%s  core %u   |   %d reps   |   1 warmup, then measured%s\n",
                ansi::bold, ansi::cyan, core, reps, ansi::reset);
    std::printf("%s%s==============================================================%s\n",
                ansi::bold, ansi::cyan, ansi::reset);
}

// A one-off informational line under the banner (e.g. a build parameter).
inline void note(const std::string& msg) {
    std::printf("  %s%s%s\n", ansi::dim, msg.c_str(), ansi::reset);
}

// Pin + isolate the calling thread to `core`, printing the outcome (yellow on failure).
inline void pin_and_isolate(unsigned core) {
    if (bench::pin_to_core(core)) std::printf("  - pinned to core %u\n", core);
    else std::printf("  %s- could not pin to core %u%s\n", ansi::yellow, core, ansi::reset);
    if (bench::isolate_core())    std::printf("  - raised to real-time priority (isolating the core)\n");
    else std::printf("  %s- could not raise priority (run as admin/root for clean numbers)%s\n",
                     ansi::yellow, ansi::reset);
}

// One scenario's result block. `msgs_per_rep` is the workload size; `reps` the
// number of replays timed; `wall_s` the total measured wall time.
inline void result(const char* scenario, std::uint64_t msgs_per_rep, int reps,
                    double wall_s, std::uint32_t resting) {
    const std::uint64_t total = msgs_per_rep * static_cast<std::uint64_t>(reps);
    const double mps = wall_s > 0.0 ? double(total) / wall_s / 1e6 : 0.0;
    std::printf("\n  %s--- %s ------------------------------------%s\n",
                ansi::bold, scenario, ansi::reset);
    std::printf("      messages      %s x %d reps = %s\n",
                commas(msgs_per_rep).c_str(), reps, commas(total).c_str());
    std::printf("      wall time     %.4f s\n", wall_s);
    std::printf("      %sTHROUGHPUT    %.2f M msgs/s%s\n", ansi::green, mps, ansi::reset);
    std::printf("      resting book  %s  (sink)\n", commas(resting).c_str());
    std::fflush(stdout);
}

inline void error(const std::string& msg) {
    std::printf("  %s! %s%s\n", ansi::red, msg.c_str(), ansi::reset);
}

}  // namespace bench::log
