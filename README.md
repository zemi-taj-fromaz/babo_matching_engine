# babo_matching_engine

`babo` is an **O(1)-cancel** limit-order-book matching engine, benchmarked
head-to-head against [liquibook](https://github.com/enewhuis/liquibook) (a
well-known reference engine) under a single **C-ABI plugin contract** — so both
engines are driven by identical inputs and scored on identical outputs, on
Windows, Linux, or macOS, in one run.

Scored three ways: **throughput**, **correctness** (SHA-256 of the report stream
must match a reference), and an **anti-cheat state audit**.

### Headline result

| Scenario | liquibook | babo | Speedup |
|---|---|---|---|
| static | 1.9 | 6.1 | **~3×** (grows with book depth) |
| normal | 3.8 | 4.7 | ~1.2× |
| swing-25 | 2.5 | 4.9 | ~2× |

*M msgs/s, 1M-message workload.* The mechanism: liquibook cancels are **O(n)** (a
`find_on_market` scan); babo's are **O(1)** (a cache-aware "PIN" structure). On a
cancel-heavy *static* book the gap widens sharply with depth — the O(n) vs O(1)
difference made visible.

---

## Quick start

**Prereqs:** CMake ≥ 3.23, a C++20 compiler (MSVC 19.3+, clang 14+, or gcc 11+),
and internet on the *first* configure (CMake fetches googletest and SPSCQueue).
No other dependencies — the perf binaries' colored output is a self-contained
header.

```bash
# 1. configure (Release is required for meaningful numbers)
cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release

# 2. build everything (both engines, adapters, perf binaries, tests)
cmake --build cmake-build-release -j
```

`cmake-build-release` is only an example build-directory name. You may choose
any directory with CMake's `-B` option; use that same path anywhere the examples
refer to `cmake-build-release`.

> **Windows + LLVM/clang:** add `-DCMAKE_RC_COMPILER="C:/Program Files/LLVM/bin/llvm-rc.exe"` to the configure step.

The default build contains one canonical babobook configuration: pull-based depth
and PIN capacity 64. `-DBABO_BUILD_PIN_SWEEP=ON` optionally adds the non-default
capacity binaries; no duplicate no-depth matrix is built because the optimized
pull-based depth showed no meaningful throughput difference.

That's it. The **easiest way to see it work** is the standalone perf binaries
(next section) — no scripts, no setup, just run one.

---

## Use babobook as a library

`babo_basic_book` is a small integration example showing book construction, a
synchronous listener, resting orders, a maker-price fill, cancellation, and
pull-based depth:

```bash
cmake --build cmake-build-release --target babo_basic_book
./cmake-build-release/libs/babobook/examples/babo_basic_book
```

Output:

```text
accept  id=1
accept  id=2

after resting orders
bids: 100@10(1)
asks: 105@7(1)
accept  id=3
fill    maker=1 taker=3 qty=4 price=100

after match
bids: 100@6(1)
asks: 105@7(1)
cancel  id=2

after cancel
bids: 100@6(1)
asks: empty
```

The example source and its consumer-side CMake setup are in
[`libs/babobook/examples/`](libs/babobook/examples/). `babobook` itself is a
header-only CMake target; linking it propagates the include path and C++20
requirement without adding a library binary.
Complexity guarantees, the precise meaning of O(1) cancel, threading and
reentrancy rules, ownership/pool lifetime, numeric limits, listener ordering, and
depth snapshot lifetime are documented in [`docs/API_CONTRACT.md`](docs/API_CONTRACT.md).

---

## Try it: the perf binaries (easiest path)

Two self-contained micro-benchmarks live in `<build>/perf/`. They link an engine
**directly** (no plugin boundary), pin themselves to an isolated CPU core, replay
a deterministic workload, and print a clean throughput report.

| Binary | Engine | Aggregate depth |
|---|---|---|
| `babo_perf` | babo | **on** (default) |
| `liqui_perf` | liquibook | **on** (default) |

```bash
# run one scenario…
./cmake-build-release/perf/babo_perf --scenario normal

# …or every scenario, fewer reps for a quick pass
./cmake-build-release/perf/liqui_perf --scenario all --reps 10
```

Options: `--scenario static|normal|swing25|flash_crash_40|flash_crash_60|all`
(default `normal`), `--reps N` (default 100). You can also pass a pre-generated
`workload.bin` as the first argument.

Example output:

```
==============================================================
  babobook   |   depth ON
  core 5   |   100 reps   |   1 warmup, then measured
==============================================================
  - pinned to core 5
  - raised to real-time priority (isolating the core)

  --- normal ------------------------------------
      messages      1,996,097 x 100 reps = 199,609,700
      wall time     17.5357 s
      THROUGHPUT    11.38 M msgs/s
      resting book  5,500  (sink)
```

> **Pinning:** the binaries pin to **core 5** and raise real-time priority. If you
> see `could not raise priority`, run as admin/root for the cleanest numbers (it
> still works without). To change the core, edit `kBenchCore` in the `perf/*.cpp`.
> **macOS:** affinity is a best-effort hint (Mach `THREAD_AFFINITY_POLICY`) —
> honored on Intel Macs, ignored on Apple Silicon, so `could not pin to core 5`
> there is expected and harmless; best-of-N reps carry the rest. `-march=native`
> falls back to `-mcpu=native` automatically on Apple Silicon clang.

### Optional build: the PIN-capacity sweep

Pull-based depth is part of the canonical build. The optional flag builds
`babo_cap{16,32,128}_perf`; the default `babo_perf` is the capacity-64 point:

```bash
cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release -DBABO_BUILD_PIN_SWEEP=ON
cmake --build cmake-build-release -j
for c in 16 32 128; do ./cmake-build-release/perf/babo_cap${c}_perf --scenario normal --reps 20; done
# compare those with the canonical capacity-64 ./cmake-build-release/perf/babo_perf
```

The measured capacities did not produce a meaningful throughput difference; the
sweep is retained as a robustness check, not as a tuned source of the headline result.

---

## Throughput matrix: market regimes × message-scale (the paper figure)

The headline report. It sweeps both engines across the four dynamic market
scenarios (`normal`, `swing25`, `flash_crash_40`, `flash_crash_60` — `static` is
skipped; it's the separate scaling experiment below) and ten message scales
(1k…2M NEW orders), running each cell best-of-N through the core-pinned perf
binaries and recording the **PEAK** throughput (per-rep min — the stable
estimator). It emits one bundle: a per-scenario table + throughput-vs-scale line
chart for both engines, a combined speedup chart, CSV/JSON, and full
CPU/compiler/git metadata.

```powershell
# Windows (~5 min on a laptop)
powershell -ExecutionPolicy Bypass -File scripts\run_market_matrix.ps1 -BuildDir cmake-build-release
```
```bash
# Linux / macOS / git-bash
bash scripts/run_market_matrix.sh --build-dir cmake-build-release
```

Flags: `--scenarios a,b`, `--counts 1000,...,2000000`, `--reps N` (default 10),
`--metric peak|median`, `--label` (defaults to `OS-CPU`). Absolute `M msgs/s`
varies by CPU/clock, so the **speedup** column is the cross-machine-comparable
figure — collect bundles from several machines and drop them side by side. The
perf binaries also gained `--count N` (the message-scale knob) and now print
`PEAK` / `median` / mean per scenario, so a single `babo_perf --scenario normal
--count 500000` is a stable one-off measurement too.

---

## Scaling curve: cancel latency vs resting-book size

The headline mechanism is **O(1) vs O(n) cancel**, and this is the experiment
that isolates it. Each engine prefills `N` non-crossing resting orders packed
into a fixed number of price levels `L` (so the depth per contended price is
`N/L` and grows with `N`), then times cancelling **all N** in a fixed shuffled
order. Prefill/allocation is off the clock — only the cancel loop is measured.

- `babo_scaling` — babo cancel is O(1) (id→slot hash index); `ns/cancel` stays
  nearly flat (its gentle rise is pure cache-hierarchy latency as the working
  set outgrows L2/L3).
- `liqui_scaling` — liquibook cancels via `find_on_market`, which rescans the
  contended price level, so `ns/cancel` climbs O(depth) and the gap explodes.

Both are default-built, core-pinned binaries (same options: `--sizes a,b,c`,
`--levels L`, `--reps R`, `--max M`). Run them directly, or use the portable
runner, which sweeps both engines, merges to CSV/JSON/Markdown, draws a
dependency-free log-log SVG, and bundles it with full CPU/compiler/git metadata:

```powershell
# Windows
powershell -ExecutionPolicy Bypass -File scripts\run_scaling.ps1 -BuildDir cmake-build-release
```
```bash
# Linux / macOS / git-bash
bash scripts/run_scaling.sh --build-dir cmake-build-release
```

`--label` defaults to `OS-CPU model` (e.g. `Windows-AMD Ryzen 7 7730U`); pass
one only to override it.
Defaults sweep `N` = 1k…200k over `L=64` levels, best of 3 reps (a few seconds;
liquibook dominates the runtime at the deep end). Add `--max 1000000` for the
hero point, or `--levels 128` / `--sizes ...` to reshape the curve. The shareable
ZIP lands under `<build>/perf/results/`. Absolute `ns/cancel` varies by CPU, so
the **speedup column** (liquibook ns ÷ babo ns) is the cross-machine-comparable
figure — which is why collecting bundles from several machines is worthwhile.

---

## Full benchmark: throughput + correctness (the harness)

The repeatable, apples-to-apples comparison runs through the **plugin harness**.
Each engine is a shared library exporting the C functions in
`benchmark/api/matching_engine_api.h`; the harness `dlopen`s it, replays a
deterministic workload one message at a time, drains the engine's report stream
over a lock-free transport on an adjacent core, hashes it (SHA-256), and compares
to a **reference hash**.

Two scripts automate the whole flow. Each ships as `.ps1` (Windows) **and** `.sh`
(Linux/macOS/git-bash) with identical behavior, and takes the build's
**benchmark dir** (`<build>/benchmark`) as an argument (auto-detected if omitted).

### 1. Generate correctness references

References are **machine-specific** (the workload generator uses floating-point
`exp`/`cos`/`log`/`pow`, which differ across CPUs), so regenerate them on the
machine you measure on. The script writes them from the trusted **liquibook**
baseline and verifies **babo** reproduces them byte-for-byte on all five scenarios.

```powershell
# Windows
powershell -ExecutionPolicy Bypass -File scripts\regen_references.ps1 -BenchDir cmake-build-release\benchmark
```
```bash
# Linux / macOS / git-bash
scripts/regen_references.sh --bench-dir cmake-build-release/benchmark
```

A `PASS`/`VALID` on every scenario means babo is provably identical to the reference.

### 2. Compare engines (throughput)

Runs all five scenarios for the engine under test vs the liquibook baseline (best
of N reps), and writes a console table plus a **CSV** and paste-ready **Markdown**
under `<benchmark>/results/`.

```powershell
# Windows
powershell -ExecutionPolicy Bypass -File scripts\compare_engines.ps1 -Engine adapters\babobook_adapter.dll -BenchDir cmake-build-release\benchmark
```
```bash
# Linux / macOS / git-bash
scripts/compare_engines.sh --engine adapters/babobook_adapter.so --bench-dir cmake-build-release/benchmark
```

Options: `--engine` (adapter under test, relative to the benchmark dir),
`--baseline` (defaults to the built liquibook), `--bench-dir`, `--count` (default
1,000,000), `--reps` (default 3).

`babobook_adapter` and `liquibook_adapter` are the single canonical adapter builds.

**Note:** `static` at 1M is slow for liquibook *by design* (the O(n) collapse).
Use `--reps 1` for a quick first pass.

---

## Tests

Two GoogleTest binaries, registered with CTest:

```bash
ctest --test-dir cmake-build-release --output-on-failure
# or run one binary directly:
./cmake-build-release/test/babo_test/babo_unit
```

- `babo_unit` — babo engine tests (built with depth **on**, so it exercises the
  full engine including the derived-depth walk).
- `liqui_unit` — the upstream liquibook suite, treated as a frozen baseline.

---

## Anti-cheat audit (run once per engine)

Not a speed metric — a **pass/fail honesty certification**. An engine could
hardcode the report stream to reproduce the hash, but it can't answer *live*
best-bid / best-ask / depth-at-price queries about a book it never maintained.
`--mode audit` replays through a trusted baseline and compares that baseline's
query answers, at 64 **unpredictable** probe indices, to the engine-under-test's.

```bash
cd cmake-build-release/benchmark
./harness --engine ./babobook_adapter.so --scenario normal --mode audit
```

A `perf` run is `VALID` on correctness alone; an `audit` run additionally requires
the state audit to `PASS`.

---

## Layout

| Path | What |
|---|---|
| `libs/babobook/` | the header-only **babo** engine and its always-built integration example |
| `libs/liquibook/` | the vendored reference engine (frozen) |
| `benchmark/` | the plugin **harness** + the C-ABI contract (`api/matching_engine_api.h`) |
| `benchmark/adapters/` | each order book wrapped behind the ABI as a canonical shared lib |
| `perf/` | two canonical, core-pinned throughput binaries plus opt-in capacity variants |
| `scripts/` | correctness/ABI runners plus throughput-matrix and cancel-scaling result runners |
| `test/` | unit tests (`babo_unit`, `liqui_unit`) |
| `docs/API_CONTRACT.md` | public complexity, threading, lifetime, numeric, and listener contract |

### How babo is fast

- **`data_structures/pin_node.h`** — a Priority-Indicated Node: fixed-capacity
  inline slots, intrusive links, **O(1) insert/erase** (slots never move). This is
  the cache-aware structure behind O(1) cancel, vs liquibook's O(n)
  `find_on_market` scan.
- **`data_structures/narb_tree.h`** — per-side price-level tree with a threaded
  best-first iterator; the source of truth for ordered price levels.
- **`book/matching_book.h`** — the matching core. Depth is **derived** from the
  tree on demand (see `depth()`), not maintained eagerly — so publishing depth
  costs the hot path nothing.

### Threading and pool lifetime

`matching_book` is a single-threaded matching core. Its PIN-node and price-level
pools are process-wide, allocation-reusing singletons and are intentionally not
synchronized. All book operations and book construction/destruction in one
process must therefore be serialized on one matching thread. Multiple book
instances are supported when that same thread owns them; concurrently operating
books on different threads are not.

Books should have ordinary scoped lifetime and be destroyed before process/module
shutdown. A `narb_tree` destructor returns its live nodes and level descriptors to
the shared pools, while the pools retain their allocated blocks for low-latency
reuse until process exit. Do not create a `matching_book` with static-storage
duration.

---

## TL;DR

1. **Build:** `cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release && cmake --build cmake-build-release -j`
2. **Try it:** run both `babo_perf --scenario all` and `liqui_perf --scenario all`.
3. **Compare rigorously:** `regen_references` then `compare_engines` (`--bench-dir <build>/benchmark`).
4. **Verify honesty (once):** `harness --engine ./babobook_adapter.<so|dll|dylib> --mode audit`.
5. **Tests:** `ctest --test-dir cmake-build-release --output-on-failure`.
