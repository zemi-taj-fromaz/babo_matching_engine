// Shared, engine-agnostic surface for the A/B comparison harness.
//
// Each engine lives in its own translation unit (run_babo.cpp / run_liqui.cpp)
// so their headers never collide -- babobook and liquibook both ship
// <simple/simple_order.h>, so they must not be on the include path together.
// The comparison main sees only this header (no engine types), and links the two
// engine TUs. Each engine exposes:
//   run_*    : timed replay (matching only, no report recording) -> best of N reps
//   verify_* : one untimed replay recording trades -> order-sensitive stream hash
// Timing excludes report recording so the measured window is matcher work, and it
// is symmetric across engines; correctness is checked separately via verify_*.
#pragma once

#include "workload.hpp"

#include <cstdint>
#include <vector>

namespace bench {

struct Throughput { double best_sec; std::uint64_t msgs; };
struct Oracle     { std::uint64_t trade_hash; std::uint64_t trade_count; };

// Order-sensitive FNV-1a fold of the trade stream: same trades in the same order
// -> same hash. The common id space is the harness order id, so babo (whose
// order id we set directly) and liquibook (mapped back from its internal id) hash
// identically iff they produce identical executions.
struct TradeHash {
  std::uint64_t h = 1469598103934665603ull;
  std::uint64_t n = 0;
  inline void mix(std::uint64_t x) { h ^= x; h *= 1099511628211ull; }
  inline void add(std::uint64_t maker, std::uint64_t taker, std::int64_t price, std::uint32_t qty) {
    mix(maker); mix(taker); mix((std::uint64_t)price); mix(qty); ++n;
  }
};

Throughput run_babo (const std::vector<Msg>& msgs, int reps);
Throughput run_liqui(const std::vector<Msg>& msgs, int reps);
Oracle     verify_babo (const std::vector<Msg>& msgs);
Oracle     verify_liqui(const std::vector<Msg>& msgs);

} // namespace bench
