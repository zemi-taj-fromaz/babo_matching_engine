# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

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

C++20, CMake ≥ 3.23, GoogleTest + spdlog + rigtorp/SPSCQueue pulled via
`FetchContent`. Release is required for meaningful numbers.

```bash
cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release -j            # builds everything
# or a subset:
cmake --build cmake-build-release --target harness generator liquibook_adapter babobook_adapter
```

Depth is a **compile-time toggle** (`BABO_NO_DEPTH` / `LIQUI_NO_DEPTH`), so each
engine ships two adapters and two perf binaries from one source: `*_adapter` /
`*_perf` are depth-OFF (lean, the fair head-to-head), `*_depth_adapter` /
`*_depth_perf` are depth-ON. spdlog is FetchContent'd and used only by the perf
binaries for clean colored output (`perf/bench_log.h`).

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
certification. The four standalone core-pinned `perf/` binaries (`babo_perf`,
`babo_depth_perf`, `liqui_perf`, `liqui_depth_perf`) link the engine directly with
no adapter/shared-lib boundary and report throughput via spdlog; they are the
depth-on/off comparison and the target for hardware-counter profiling.

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
  Built twice each (depth on/off): `{babobook,liquibook}_adapter` are depth-off,
  `{babobook,liquibook}_depth_adapter` depth-on. `liqui_book_type.h` selects the
  liquibook book type (`SimpleOrderBook` vs a depth-free `NoDepthBook`) and is
  shared with `liqui_perf`.
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
