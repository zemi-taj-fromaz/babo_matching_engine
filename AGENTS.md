# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

## What this repo is

`babo` is an O(1)-cancel limit-order-book matching engine, benchmarked head-to-head
against the vendored reference engine **liquibook** under one **C-ABI plugin
contract** (`benchmark/api/matching_engine_api.h`). Any challenger engine wrapped
behind that ABI as a shared library can be `dlopen`'d by the harness and scored on
throughput, correctness (SHA-256 of the report stream vs a reference hash), and an
anti-cheat state audit — cross-platform, in one run.

`README.md` is the authoritative guide to the benchmark/measurement workflow; read
it before touching the harness, scripts, or scenarios. `building_diary.md` is the
author's personal design log (partly in Croatian) and records intent/TODOs, not
current state.

## Build

C++20, CMake ≥ 3.23, GoogleTest + rigtorp/SPSCQueue pulled via `FetchContent`.
Release is required for meaningful numbers.

```bash
cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release -j            # builds everything
# or a subset:
cmake --build cmake-build-release --target harness generator liquibook_adapter babobook_adapter
```

Pull-based depth is part of the single canonical build. The retired no-depth
matrix measured no meaningful difference after the optimization and is no longer
built. `babobook_adapter` / `liquibook_adapter` / `babo_perf` / `liqui_perf` are
the canonical targets. One option adds non-default capacity experiments:
`-DBABO_BUILD_PIN_SWEEP=ON` builds `babo_cap{16,32,128}_perf`; the default
`babo_perf` is capacity 64. The perf binaries print via a
dependency-free ANSI-color header (`perf/bench_log.h`) — no spdlog.

- On Windows with the LLVM/clang toolchain, add
  `-DCMAKE_RC_COMPILER="C:/Program Files/LLVM/bin/llvm-rc.exe"`.
- Multiple build dirs coexist (`cmake-build-{debug,release}{,-system}`); all are
  gitignored. Scripts auto-detect the newest `cmake-build-*/benchmark` if not told.
- Release flags (`-O3 -march=native -funroll-loops` + LTO) and all compiler policy
  live in `cmake/CompilerSettings.cmake`. **Do not add `-ffast-math`** — it breaks
  the workload generator's FP determinism and invalidates every correctness
  reference. The `generator` target is deliberately pinned to bit-stable FP via
  `target_deterministic_fp()` (no FMA contraction, no LTO).

## Test

Two GoogleTest binaries, registered with CTest via `gtest_discover_tests`:
- `babo_unit` — babo engine tests (`test/babo_test/ut_*.cpp`)
- `liqui_unit` — upstream liquibook tests, Boost.Test swapped for GoogleTest via
  `test/liqui_test/unit/boost_shim.h`; treat as a frozen baseline.

```bash
cmake --build cmake-build-release --target babo_unit liqui_unit
ctest --test-dir cmake-build-release --output-on-failure   # all
cmake-build-release/test/babo_test/babo_unit --gtest_filter='SuiteName.CaseName'  # one test
```

Both test dirs `GLOB_RECURSE` their sources, so a new `ut_*.cpp` is picked up on
reconfigure with no CMake edit.

## Benchmark workflow (the two scripts)

Each script ships as `.ps1` (Windows) and `.sh` (Linux/macOS/git-bash) with
identical behavior; both take the build's **benchmark dir** as an argument.

1. `scripts/regen_references` — generate the workload + reference hashes from the
   trusted liquibook baseline, persist them into the source tree, and verify babo
   reproduces them byte-for-byte across all five scenarios. References are
   **machine-specific** (FP `exp`/`cos`/`log`/`pow` differ across CPUs), so
   regenerate on the machine you measure on.
2. `scripts/compare_engines` — run all five scenarios for the engine under test vs
   the liquibook baseline (best of N reps), emit a console table + CSV + Markdown
   under `<benchmark>/results/`. Runs `--mode perf` only.

**Audit** (`--mode audit`) and the **perf/hardware-counter** micro-benchmarks are
intentionally *not* scripted — see README. Audit is a once-per-engine pass/fail
certification. The standalone core-pinned `perf/` binaries (`babo_perf`,
`liqui_perf` by default; `babo_cap*_perf` under the opt-in capacity flag) link the
engine directly with no adapter/shared-lib boundary and
print a colored throughput report; they are the target for hardware-counter
profiling and the depth / capacity experiments.

## Architecture

Three layers, decoupled by the C ABI:

- **Engines** (`libs/`): `babobook` (the engine under development, header-only in
  `include/`, one `.cpp`) and `liquibook_ref` (vendored, frozen). Nothing C++
  crosses the harness↔engine boundary — it's pure C ABI, which is why each module
  safely carries its own statically-linked runtime.
- **Adapters** (`benchmark/adapters/`): each engine wrapped as a `SHARED` lib
  exporting the `matching_engine_api.h` C functions (`engine_init`,
  `engine_on_new_order`, `engine_on_cancel`, `engine_on_modify`, `engine_flush`,
  `engine_query_*`). Optional exports: `engine_get_transport`, `engine_prebuild`,
  `engine_on_batch` — read the header comments before implementing these, they carry
  strict anti-cheat contracts. `template_adapter.h.cpp` is the starting skeleton.
  `{babobook,liquibook}_adapter` are the single canonical adapter targets.
  `liqui_book_type.h` defines the Liquibook book type shared with `liqui_perf`.
- **Harness** (`benchmark/src/`): `dlopen`s an adapter, replays a deterministic
  workload, drains the engine's report stream over an SPSC transport on an adjacent
  core, hashes it (`third_party/sha256.c`), and compares to the reference. Reports
  are `me_report_t` (64-byte cache line); the report transport is a vtable
  (`me_transport_t`) the engine can override.

### babo engine internals (`libs/babobook/include/`)

- `book/matching_book.h` — the matching core: `matching_book<SIZE, TRADE_CAP>`. The
  book **owns nothing**; the `narb_tree`s own all resting/parked orders by value and
  the application interacts purely by order id. Handles new/cancel/replace/market-price,
  IOC, AON, and stop orders (parked in separate `narb_tree`s keyed by stop price).
  Emits an inline lock-free trade ring plus the order/trade/book-change listeners.
  Depth is **pull-based**: `depth()` (compiled out under `BABO_NO_DEPTH`) derives the
  top-SIZE aggregate on demand by walking the tree's best-first level threads and
  reading each `price_level_descriptor`'s `_quantity`/`_count` — nothing on the hot
  path. (The old eager `Depth` + `std::map` excess tracker was removed.)
- `data_structures/narb_tree.h` — per-side price-level tree with a threaded
  (pred/succ) bidirectional best-first iterator; templated on `order_type::{BID,ASK}`.
- `data_structures/pin_node.h` — Priority-Indicated Node: fixed-capacity inline slot
  region, intrusive prev/next links, O(1) insert/erase (slots never move). This is
  the cache-aware structure that gives babo its O(1) cancel (vs liquibook's O(n)
  `find_on_market` scan — the source of the `static`-scenario speedup).
- `memory/memory_pool.h`, `simple/simple_order.h`, `book/depth.h` — arena, order
  value type, and a passive top-of-book depth **snapshot** (filled by the walk in
  `matching_book::depth()`; no per-op maintenance).

### Key contract invariants (from `matching_engine_api.h`)

- Prices are signed integer ticks; 1 tick = $0.005. Modify = cancel + reinsert
  (loses time priority). Every operation emits its report (OrderAck / Trade /
  Cancel(Ack|Reject) / Modify(Ack|Reject)); the harness never synthesizes reports.
- `engine_flush()` is the measured pipeline barrier — it must not return until all
  delivered messages are matched and all reports pushed. `engine_query_*` must
  reflect every message delivered before the call. Struct sizes are `static_assert`ed
  (32/16/32/64/40 bytes) — changing a struct layout is an ABI break.

## Project status (snapshot 2026-07-11)

### Done in the recent arc
- **Derived (lazy) depth.** Removed the eager `book::Depth` + `std::map` excess
  tracker. `depth()` now builds a snapshot on demand by walking the tree's top-SIZE
  levels best-first, reading each `price_level_descriptor`'s `_quantity` and new
  `_count` (repurposed the dead `_depth` field; maintained by `++/--` in
  `narb_tree::place_order`/`erase`). Hot path pays ~nothing; the eager `std::map`
  per-deep-order allocation is gone. Depth/bbo **push** listeners removed — depth is
  pull-only. `ut_depth.cpp` trimmed to the walk-based tests; `changed_checker.h`
  deleted. Result: depth-on ≈ depth-off in throughput.
- **One canonical depth build.** Pull-based depth measured the same as the old
  no-depth experiment within run-to-run variation, so the duplicate target matrix
  was removed. `-DBABO_BUILD_PIN_SWEEP=ON` adds only `babo_cap{16,32,128}_perf`;
  capacity 64 is the canonical `babo_perf`.
- **Portable perf bundle.** `scripts/run_portable_perf.{py,ps1,sh}` runs both
  canonical books across all five scenarios and emits shareable Markdown/CSV/JSON,
  raw outputs, compiler/OS/CPU/build metadata, binary hashes, manifest, and ZIP.
- **Perf output.** `perf/bench_log.h` — self-contained ANSI-color reporter (spdlog
  was tried and **removed**: its bundled fmt fails clang's `consteval`). Banner shows
  engine / depth / core / reps; babo also prints `pin_node capacity`.
- **Docs.** README + AGENTS.md rewritten for the above.

### To do next session (in order)
1. **Rebuild from a fresh CMake configure** — NOT done since the spdlog removal and
   the depth-default change. Confirm a clean build + `ctest` green. **Gating item.**
2. **Refresh the numbers:** run the portable 100-rep `babo_perf` / `liqui_perf`
   matrix across compilers and operating systems; optionally run
   `-DBABO_BUILD_PIN_SWEEP=ON` as a robustness check. Then `regen_references` +
   `compare_engines` and verify all hashes.
3. **The paper / write-up** (portfolio, not academic — frame accordingly):
   mechanism (O(1) PIN cancel vs O(n) `find_on_market`), the derived-depth story
   (nearly-free depth babo can do and liquibook structurally can't), the anti-cheat
   SHA-256 methodology (the credibility anchor), the cross-platform compiler/OS
   matrix, and the **scaling curve** (cancel throughput vs resting-book size — the money figure,
   NOT YET RUN), plus an honest threats-to-validity note.
