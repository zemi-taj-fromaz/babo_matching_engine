// Validates the workload generator against the paper's spec: from 1M NEW orders
// it should produce ~2M total messages at ~95% cancel / 20% modify / 15% IOC,
// with a price span that widens with the volatility regime.
//
// Usage: workload_stats [scenario] [num_new]
//   scenario: static | normal | swing25 | flash_crash_40 | flash_crash_60
#include "workload.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <limits>

int main(int argc, char** argv) {
  bench::WorkloadParams p;

  const char* scen = (argc > 1) ? argv[1] : "normal";
  for (const auto& s : bench::kScenarios)
    if (std::strcmp(s.name, scen) == 0) { p.target_swing = s.target_swing; break; }
  if (argc > 2) { std::uint32_t n = (std::uint32_t)std::atoll(argv[2]); if (n) p.num_new = n; }

  std::printf("scenario=%s  num_new=%u  beta=%.2f  mid0=%u ticks  seed=%llu\n",
              scen, p.num_new, p.beta, p.mid0_ticks, (unsigned long long)p.seed);

  auto msgs = bench::WorkloadGen(p).generate();

  std::size_t adds = 0, mods = 0, cans = 0, iocs = 0;
  std::uint32_t pmin = std::numeric_limits<std::uint32_t>::max(), pmax = 0;
  for (const auto& m : msgs) {
    switch (m.op) {
      case bench::Op::Add:
        ++adds; if (m.ioc) ++iocs;
        if (m.price < pmin) pmin = m.price;
        if (m.price > pmax) pmax = m.price;
        break;
      case bench::Op::Modify: ++mods; break;
      case bench::Op::Cancel: ++cans; break;
    }
  }
  const std::size_t total   = msgs.size();
  const std::size_t non_ioc = adds - iocs;

  std::printf("total messages : %zu\n", total);
  std::printf("  adds         : %zu\n", adds);
  std::printf("  modifies     : %zu  (%.1f%% of non-IOC)\n", mods, 100.0 * mods / non_ioc);
  std::printf("  cancels      : %zu  (%.1f%% of non-IOC)\n", cans, 100.0 * cans / non_ioc);
  std::printf("  IOC adds     : %zu  (%.1f%% of adds)\n", iocs, 100.0 * iocs / adds);
  std::printf("expansion      : %.2fx (total / num_new)\n", (double)total / p.num_new);
  const double span = (double)(pmax - pmin);
  std::printf("price span     : %u..%u ticks = %.0f ticks (%.1f%% of mid0)\n",
              pmin, pmax, span, 100.0 * span / p.mid0_ticks);
  return 0;
}
