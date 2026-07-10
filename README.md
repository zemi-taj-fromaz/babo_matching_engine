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
and internet on the *first* configure (CMake fetches googletest, spdlog, and
SPSCQueue).

```bash
# 1. configure (Release is required for meaningful numbers)
cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release

# 2. build everything (all engines, adapters, perf binaries, tests)
cmake --build cmake-build-release -j
```

> **Windows + LLVM/clang:** add `-DCMAKE_RC_COMPILER="C:/Program Files/LLVM/bin/llvm-rc.exe"` to the configure step.

That's it. The **easiest way to see it work** is the standalone perf binaries
(next section) — no scripts, no setup, just run one.

---

## Try it: the perf binaries (easiest path)

Four self-contained micro-benchmarks live in `<build>/perf/`. They link an engine
**directly** (no plugin boundary), pin themselves to an isolated CPU core, replay
a deterministic workload, and print a clean throughput report.

| Binary | Engine | Aggregate depth |
|---|---|---|
| `babo_perf` | babo | **off** (lean) |
| `babo_depth_perf` | babo | **on** |
| `liqui_perf` | liquibook | **off** (lean) |
| `liqui_depth_perf` | liquibook | **on** |

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
  babobook   |   depth OFF
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

### The depth comparison (a project highlight)

Run all four and compare `babo_perf` vs `babo_depth_perf`, and `liqui_perf` vs
`liqui_depth_perf`. You'll see maintaining an aggregate depth **barely dents
liquibook but is nearly free on babo** — because babo doesn't maintain depth
eagerly: it **derives** it on demand by walking the tree's top-N price levels
(O(N) per query, nothing on the hot path), whereas liquibook's `std::multimap`
book can't enumerate ordered levels cheaply and pays per-op. Depth is a
compile-time toggle (`-DBABO_NO_DEPTH` / `-DLIQUI_NO_DEPTH`), so the two builds
of each engine come from the exact same source.

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

> The adapters come in depth-off (`babobook_adapter`, `liquibook_adapter`) and
> depth-on (`babobook_depth_adapter`, `liquibook_depth_adapter`) builds — point
> `--engine` / `--baseline` at whichever pair you want to compare.

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
| `libs/babobook/` | the **babo** engine — `matching_book<SIZE, TRADE_CAP>`, header-only + one `.cpp` |
| `libs/liquibook/` | the vendored reference engine (frozen) |
| `benchmark/` | the plugin **harness** + the C-ABI contract (`api/matching_engine_api.h`) |
| `benchmark/adapters/` | each engine wrapped behind the ABI as a shared lib (depth-on/off builds) |
| `perf/` | the four standalone, core-pinned throughput binaries |
| `scripts/` | `regen_references` + `compare_engines` (`.ps1` / `.sh`) |
| `test/` | unit tests (`babo_unit`, `liqui_unit`) |

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

---

## TL;DR

1. **Build:** `cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release && cmake --build cmake-build-release -j`
2. **Try it:** `./cmake-build-release/perf/babo_perf --scenario all` (and the other three perf binaries).
3. **Compare rigorously:** `regen_references` then `compare_engines` (`--bench-dir <build>/benchmark`).
4. **Verify honesty (once):** `harness --engine ./babobook_adapter.<so|dll|dylib> --mode audit`.
5. **Tests:** `ctest --test-dir cmake-build-release --output-on-failure`.
