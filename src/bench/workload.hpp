// Deterministic, engine-agnostic workload generator replicating the methodology
// of Yoon, "The World's Fastest Matching Engine Algorithm" (arXiv 2606.01183), §6.1.
//
// Each NEW order is calibrated to a liquid equity (NVIDIA) and expanded into a
// short lifetime trace: add -> optional modify -> cancel (or IOC that resolves
// immediately). Limit prices are drawn from a power-law depth distribution
// (exponent beta) around a mid that evolves per order via geometric Brownian
// motion (GBM). A fixed seed makes the whole stream reproducible across engines.
//
// The output is a flat, time-ordered array of Msg to be replayed back-to-back.
// This models the workload only; it does NOT match orders -- the engine under
// test does that. (So a scheduled cancel may target an order that already
// traded; engines treat that as a cancel-reject, which is realistic.)
//
// Deviations from the paper, kept deliberately simple for a from-scratch replica:
//  * GBM per-order sigma is derived from a target swing as sigma/sqrt(N) rather
//    than by tuning dt to a realized span; close enough to reproduce the regimes.
//  * Order arrivals are evenly spaced in virtual time (not Poisson); only the
//    spacing-to-lifetime ratio matters, and it sets the resting-book depth.
#pragma once

#include <cstdint>
#include <cmath>
#include <queue>
#include <random>
#include <vector>

namespace bench {

enum class Op : std::uint8_t { Add, Modify, Cancel };

struct Msg {
  Op            op;
  std::uint32_t id;      // order id (unique per NEW)
  bool          is_buy;
  std::uint32_t price;   // limit price in ticks (Add/Modify); unused for Cancel
  std::uint32_t qty;     // Add/Modify; unused for Cancel
  bool          ioc;     // Add only: immediate-or-cancel (no resting residual)
};

// One of the five volatility regimes from the paper (Table 6).
struct Scenario {
  const char* name;
  double      target_swing;   // fractional price swing over the burst (0 = static)
};

inline constexpr Scenario kScenarios[] = {
  {"static",         0.00},
  {"normal",         0.02},
  {"swing25",        0.25},
  {"flash_crash_40", 0.40},
  {"flash_crash_60", 0.60},
};

struct WorkloadParams {
  std::uint32_t num_new        = 1'000'000;  // NEW orders -> ~2M total messages
  double        beta           = 2.23;       // power-law depth exponent (NVDA fit)
  std::uint32_t mid0_ticks     = 33504;      // $167.52 / $0.005 tick
  std::uint32_t qty_min        = 1;
  std::uint32_t qty_max        = 100;
  double        p_ioc          = 0.15;
  double        p_modify        = 0.20;      // of non-IOC orders
  double        p_cancel        = 0.95;      // of non-IOC orders
  double        median_life_ms  = 0.431;     // non-IOC lifetime median
  std::uint32_t live_target     = 2000;      // ~concurrent resting orders (sets arrival spacing)
  // Cap the power-law tail at the book's finite depth. 765 ticks/side gives a
  // ~1530-tick (4.6%) static active-price span, matching the paper's calibration
  // (its "realized span" for the zero-drift static regime is 1,529 ticks, 4.6%).
  std::uint32_t max_offset      = 765;       // ticks from mid
  std::uint64_t seed            = 12345;
  double        target_swing    = 0.02;      // GBM swing (from Scenario); 0 = static
};

class WorkloadGen {
public:
  explicit WorkloadGen(WorkloadParams p) : p_(p) {}

  std::vector<Msg> generate() const {
    std::mt19937_64 rng(p_.seed);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    std::uniform_int_distribution<std::uint32_t> qtyd(p_.qty_min, p_.qty_max);
    std::normal_distribution<double> gauss(0.0, 1.0);

    const double mean_life_ms = p_.median_life_ms / std::log(2.0);  // exp mean from median
    std::exponential_distribution<double> lifed(1.0 / mean_life_ms);

    // Even arrival spacing chosen so ~live_target orders coexist (mean_life/dt).
    const double dt_arrival_ms = mean_life_ms / std::max(1u, p_.live_target);
    // Per-order GBM sigma so the cumulative 1-sigma log-return ~ target swing.
    const double sigma_step =
        (p_.target_swing > 0.0) ? p_.target_swing / std::sqrt((double)p_.num_new) : 0.0;

    // Future modify/cancel events, ordered by virtual time (min-heap).
    struct Ev { double t; Op op; std::uint32_t id; bool is_buy; std::uint32_t price; std::uint32_t qty; };
    auto later = [](const Ev& a, const Ev& b) { return a.t > b.t; };
    std::priority_queue<Ev, std::vector<Ev>, decltype(later)> heap(later);

    std::vector<Msg> out;
    out.reserve((std::size_t)p_.num_new * 2);

    auto emit = [&](const Ev& e) { out.push_back(Msg{e.op, e.id, e.is_buy, e.price, e.qty, false}); };

    double mid = p_.mid0_ticks;
    for (std::uint32_t i = 0; i < p_.num_new; ++i) {
      const double arrival = i * dt_arrival_ms;

      // Flush all scheduled events that come due at/before this arrival, in order.
      while (!heap.empty() && heap.top().t <= arrival) { emit(heap.top()); heap.pop(); }

      // Evolve the mid one GBM step per NEW order.
      if (sigma_step > 0.0)
        mid *= std::exp(-0.5 * sigma_step * sigma_step + sigma_step * gauss(rng));
      std::uint32_t midt = (std::uint32_t)std::lround(mid);
      if (midt < 1) midt = 1;

      const std::uint32_t id     = i + 1;
      const bool          is_buy = uni(rng) < 0.5;
      const std::uint32_t qty    = qtyd(rng);

      // Power-law offset from the mid (Pareto inverse-CDF, x_min = 1 tick).
      const double u   = uni(rng);
      double       off = std::pow(1.0 - u, -1.0 / (p_.beta - 1.0));
      std::uint32_t offset = (std::uint32_t)std::min(off, (double)p_.max_offset);
      if (offset < 1) offset = 1;

      const bool ioc = uni(rng) < p_.p_ioc;
      // IOC orders are marketable (cross the touch); passive orders rest near mid.
      std::uint32_t price;
      if (ioc) price = is_buy ? midt + offset : (midt > offset ? midt - offset : 1);
      else     price = is_buy ? (midt > offset ? midt - offset : 1) : midt + offset;

      out.push_back(Msg{Op::Add, id, is_buy, price, qty, ioc});
      if (ioc) continue;   // IOC resolves at add time; no lifetime

      const double life = lifed(rng);
      // Active quote management: a small in-flight modify, then an eventual cancel.
      if (uni(rng) < p_.p_modify) {
        const std::uint32_t np = (uni(rng) < 0.5) ? price + 1 : (price > 1 ? price - 1 : price + 1);
        heap.push(Ev{arrival + life * uni(rng), Op::Modify, id, is_buy, np, qtyd(rng)});
      }
      if (uni(rng) < p_.p_cancel)
        heap.push(Ev{arrival + life, Op::Cancel, id, is_buy, 0, 0});
    }

    while (!heap.empty()) { emit(heap.top()); heap.pop(); }   // drain the tail
    return out;
  }

private:
  WorkloadParams p_;
};

} // namespace bench
