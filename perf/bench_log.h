#pragma once
// bench_log.h — clean, colored console output for the standalone perf binaries,
// backed by spdlog. Header-only. Keeps a minimal "message only" pattern so the
// output reads as a report, not a log. ASCII-only glyphs so it renders on every
// console (classic Windows console, Windows Terminal, Linux, macOS).

#include "bench_util.h"      // bench::pin_to_core / bench::isolate_core

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdint>
#include <string>

namespace bench::log {

// One colored stdout logger, message-only pattern (info = default, warn = yellow,
// error = red). Call once at program start.
inline void init() {
    auto logger = spdlog::stdout_color_mt("perf");
    logger->set_pattern("%^%v%$");
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::trace);
}

// 1234567 -> "1,234,567"
inline std::string commas(std::uint64_t v) {
    std::string s = std::to_string(v);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(i, ",");
    return s;
}

// Header block: engine name, depth config, pinned core, rep count.
inline void banner(const char* engine, bool depth_on, unsigned core, int reps) {
    spdlog::info("");
    spdlog::info("==============================================================");
    spdlog::info("  {}   |   depth {}", engine, depth_on ? "ON" : "OFF");
    spdlog::info("  core {}   |   {} reps   |   1 warmup, then measured", core, reps);
    spdlog::info("==============================================================");
}

// Pin + isolate the calling thread to `core`, logging the outcome (warn on failure).
inline void pin_and_isolate(unsigned core) {
    if (bench::pin_to_core(core)) spdlog::info("  - pinned to core {}", core);
    else                          spdlog::warn("  - could not pin to core {}", core);
    if (bench::isolate_core())    spdlog::info("  - raised to real-time priority (isolating the core)");
    else                          spdlog::warn("  - could not raise priority (run as admin/root for clean numbers)");
}

// One scenario's result block. `msgs_per_rep` is the workload size; `reps` the
// number of replays timed; `wall_s` the total measured wall time.
inline void result(const char* scenario, std::uint64_t msgs_per_rep, int reps,
                    double wall_s, std::uint32_t resting) {
    const std::uint64_t total = msgs_per_rep * static_cast<std::uint64_t>(reps);
    const double mps = wall_s > 0.0 ? double(total) / wall_s / 1e6 : 0.0;
    spdlog::info("");
    spdlog::info("  --- {} ------------------------------------", scenario);
    spdlog::info("      messages      {} x {} reps = {}", commas(msgs_per_rep), reps, commas(total));
    spdlog::info("      wall time     {:.4f} s", wall_s);
    spdlog::info("      THROUGHPUT    {:.2f} M msgs/s", mps);
    spdlog::info("      resting book  {}  (sink)", commas(resting));
}

inline void error(const std::string& msg) { spdlog::error("  ! {}", msg); }

}  // namespace bench::log
