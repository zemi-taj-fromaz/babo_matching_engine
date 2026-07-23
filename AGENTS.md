# AGENTS.md

Guidance for coding agents working in this repository.

## Project

`babo` is a C++20 limit-order-book matching engine built around O(1) average
order lookup and unlink within a populated price level. The repository compares
it with the vendored Liquibook engine through a shared C ABI and two separate
measurement layers:

- `benchmark/` certifies adapter behavior through report-stream SHA-256 hashes
  and an anti-cheat state audit.
- `perf/` directly links each engine for matching-path throughput, profiling,
  and cancel-scaling experiments.

Do not treat a `perf/` result as correctness proof. Do not treat harness
throughput as the primary matching-core measurement.

## Sources of truth

Read the relevant document before changing a subsystem:

- `README.md` — project overview and canonical build/test/measurement workflow.
- `benchmark/api/matching_engine_api.h` — authoritative public C ABI and its
  behavioral contracts.
- `scripts/README.md` — purpose and usage of the four supported script workflows.
- `perf/README.md` — performance methodology, provenance, timed boundaries,
  affinity behavior, and limitations.
- `THIRD_PARTY_NOTICES.md` — dependency provenance and license locations.

Generated result reports identify their generator in the header. Do not edit
generated Markdown, CSV, JSON, SVG, manifests, or paper data by hand.

## Build

CMake 3.23 or newer and a C++20 compiler are required. CMake downloads
GoogleTest and Rigtorp SPSCQueue on the first configure.

```bash
cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release -j
```

`cmake-build-release` is only an example directory name; commands must use the
actual path passed to `cmake -B`.

Useful targets:

```bash
cmake --build <build-dir> --target \
  babo_unit liqui_unit \
  harness generator babobook_adapter liquibook_adapter \
  babo_perf liqui_perf babo_scaling liqui_scaling
```

Release builds are mandatory for publishable performance numbers. Compiler and
optimization policy belongs in `cmake/CompilerSettings.cmake`.

On Windows with LLVM/Clang, configuration may require:

```text
-DCMAKE_RC_COMPILER="C:/Program Files/LLVM/bin/llvm-rc.exe"
```

`-DBABO_BUILD_PIN_SWEEP=ON` adds the non-default
`babo_cap{16,32,128}_perf` targets. The canonical `babo_perf` uses capacity 64.

## Tests

```bash
cmake --build <build-dir> --target babo_unit liqui_unit
ctest --test-dir <build-dir> --output-on-failure
```

- `babo_unit` tests Babobook.
- `liqui_unit` is the adapted upstream Liquibook suite and should remain a
  frozen baseline.

Both test directories discover their sources with `GLOB_RECURSE`; adding a new
test source requires a CMake reconfigure but normally no CMake list edit.

## Supported scripts

Each public workflow has a PowerShell launcher for Windows and a shell launcher
for macOS/Linux where applicable:

- `regen_references` — generate trusted Liquibook correctness hashes and verify
  Babobook. It intentionally changes
  `benchmark/reference/correctness_hash.txt`.
- `compare_engines` — exercise a C-ABI adapter through the harness and compare
  it with Liquibook.
- `run_market_matrix` — primary throughput matrix across dynamic regimes and
  message scales.
- `run_scaling` — cancel latency against resting-book size.

Reference hashes can be machine-specific because the deterministic generator
still depends on platform `libm` implementations. Never regenerate references
merely to make a failing engine pass.

## Repository layout

- `libs/babobook/` — header-only Babobook target, public headers, and integration
  example.
- `libs/liquibook/` — vendored reference engine; avoid modifications.
- `benchmark/adapters/` — Babobook and Liquibook shared-library C-ABI adapters.
- `benchmark/src/` — dynamic harness, transport, correctness, and audit logic.
- `benchmark/workload/` — canonical binary workload generator.
- `benchmark/third_party/` — harness-local SHA-256 and SPSC wrapper support.
- `perf/` — direct-link throughput and cancel-scaling executables.
- `scripts/` — supported cross-platform runners and result bundling.
- `paper/` — collected cross-machine result data and portfolio write-up.
- `test/` — Babobook and Liquibook GoogleTest suites.

## Babobook internals

- `book/matching_book.h` owns four trees: live bids, live asks, parked buy stops,
  and parked sell stops.
- `data_structures/narb_tree.h` stores price levels in a threaded RB tree.
- `data_structures/pin_node.h` provides stable inline order slots and intrusive
  links; locating and unlinking an order within a populated level is average
  O(1).
- Removing the final order at a price also removes its RB-tree level and
  therefore pays O(log L).
- Depth is pull-based. `depth()` derives a snapshot from the level threads;
  matching operations do not eagerly maintain a separate depth map.
- Orders are stored by value and subsequently addressed by ID.
- The PIN-node and price-level pools are unsynchronized process-wide
  singletons. Books are single-threaded, must have scoped lifetime, and must be
  destroyed before module/process shutdown.

## C ABI invariants

Nothing C++ crosses the harness/adapter boundary. Before editing adapters or ABI
types, read all comments and `static_assert`s in
`benchmark/api/matching_engine_api.h`.

In particular:

- preserve exported function signatures and structure layouts;
- every delivered operation must produce its required report;
- `engine_flush()` is a barrier and must drain all prior work and reports;
- `engine_query_*` must observe all messages delivered before the query;
- optional transport, prebuild, and batch APIs have strict anti-cheat rules;
- modify is cancel plus reinsert and loses time priority.

## Change rules

- Do not add `-ffast-math`; it changes workload generation and invalidates
  correctness references.
- Keep the canonical generator free of FMA contraction and LTO through
  `target_deterministic_fp()`.
- Do not casually modify vendored Liquibook or its tests. Any deliberate patch
  must be isolated, justified, and documented.
- Keep Babobook header-only; examples are executables that consume the interface
  target, not compiled implementation objects.
- Preserve the distinction between harness correctness and direct-link
  performance in code and documentation.
- Validate changes proportionally: compile affected targets, run focused tests,
  then run full CTest for engine or ABI changes.
- Do not publish Debug-build performance numbers.
