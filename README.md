# babo_matching_engine

`babo` is an O(1)-cancel limit-order-book matching engine. This repo benchmarks it
head-to-head against [liquibook](https://github.com/enewhuis/liquibook) (the
reference engine) under a single **C-ABI plugin contract**, so any challenger
engine can be dropped in and measured under identical conditions — on Windows,
Linux, or macOS, in one run.

## Layout

| Path | What |
|---|---|
| `libs/babobook/` | the babo engine (`matching_book<SIZE, TRADE_CAP>`) |
| `libs/liquibook/` | the reference engine, vendored |
| `benchmark/` | the plugin **harness**: replays a deterministic workload through a `dlopen`'d engine adapter and scores it (throughput + correctness + anti-cheat) |
| `benchmark/adapters/` | `babobook_adapter` / `liquibook_adapter` — each engine wrapped behind `api/matching_engine_api.h` and built as a shared lib |
| `perf/` | standalone **hardware-counter, latency, and throughput** micro-benchmarks (core-pinned) |
| `scripts/` | automation — regenerate references, compare engines |
| `test/` | unit tests (`babo_test`, `liqui_test`) |

## How the harness works

The engine is compiled into a shared library that exports the C functions in
`benchmark/api/matching_engine_api.h` (`engine_init`, `engine_on_new_order`, …).
The harness `dlopen`s it, replays a deterministic workload one message at a time,
and the engine emits its own report stream (acks + trades) over a lock-free
transport drained on an adjacent core. The harness hashes that stream (SHA-256)
and compares it to a published **reference hash**.

Two run modes (`--mode`):

- **`perf`** *(default)* — the measured run. Times the workload and reports
  **throughput**; verifies the output hash for correctness.
- **`audit`** — the **anti-cheat** run (not timed). See [Audit](#audit-anti-cheat).

Five **scenarios** model different market regimes, driven by the paper's workload
(power-law depth β=2.23 + GBM mid-price): `static`, `normal`, `swing-25`,
`swing-40`, `flash-crash`.

## Build

Configure a build dir (Release for real numbers) and build the four benchmark
targets. On Windows with the LLVM/clang toolchain, keep
`-DCMAKE_RC_COMPILER="C:/Program Files/LLVM/bin/llvm-rc.exe"`.

```bash
cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release --target harness generator liquibook_adapter babobook_adapter
```

Everything below refers to the build's **benchmark dir**:
`<build-dir>/benchmark` (e.g. `cmake-build-release-system/benchmark`).

---

## The two scripts automate the whole throughput pipeline

`scripts/regen_references` and `scripts/compare_engines` cover the entire
**perf + correctness** flow: generate the workload, produce reference hashes,
run every scenario, and emit ready-to-paste reports. Each ships in **two
flavors** — `.ps1` (Windows PowerShell) and `.sh` (Linux / macOS / git-bash) —
with identical behavior. The `.sh` versions auto-handle `harness` vs
`harness.exe` and `.so` / `.dll` / `.dylib`.

Both scripts take the **benchmark build dir** as an argument (or auto-detect the
newest `cmake-build-*/benchmark` if omitted).

### 1. Generate correctness references

References are **machine-specific** (the workload generator uses floating-point
`exp`/`cos`/`log`/`pow`, which differ across CPU architectures and optimization
levels), so you regenerate them on the machine + build you'll measure on. The
script writes them from the trusted **liquibook** baseline, persists them into the
source tree (so a rebuild's `POST_BUILD` copy doesn't revert them), and then
verifies **babo** reproduces them byte-for-byte on all five scenarios.

```powershell
# Windows
powershell -ExecutionPolicy Bypass -File scripts\regen_references.ps1 -BenchDir cmake-build-release-system\benchmark
```
```bash
# Linux / macOS / git-bash
scripts/regen_references.sh --bench-dir cmake-build-release/benchmark
```

Options: `-BenchDir`/`--bench-dir` (build's benchmark dir), `-Count`/`--count`
(default **1,000,000** — the canonical size). A `PASS`/`VALID` on every scenario
means babo is provably identical to the reference.

### 2. Compare engines (throughput)

Runs all five scenarios for the engine under test **and** the liquibook baseline
(best of N reps), then writes a console table plus a **CSV** and a paste-ready
**Markdown** report under `<benchmark>/results/`.

```powershell
# Windows
powershell -ExecutionPolicy Bypass -File scripts\compare_engines.ps1 -Engine adapters\babobook_adapter.dll -BenchDir cmake-build-release-system\benchmark
```
```bash
# Linux / macOS / git-bash
scripts/compare_engines.sh --engine adapters/babobook_adapter.so --bench-dir cmake-build-release/benchmark
```

Options: `-Engine`/`--engine` (adapter under test, relative to the benchmark
dir), `-Baseline`/`--baseline` (defaults to the built liquibook),
`-BenchDir`/`--bench-dir`, `-Count`/`--count` (default 1M), `-Reps`/`--reps`
(default 3). Example output:

```
Scenario     liquibook  babo   Speedup  Correctness
static          1.91     6.09   3.19x    PASS
normal          3.84     4.66   1.21x    PASS
swing-25        2.45     4.87   1.99x    PASS
swing-40        4.28     4.66   1.09x    PASS
flash-crash     4.82     5.27   1.09x    PASS
```

> `static` is the headline: liquibook cancels are **O(n)** (a `find_on_market`
> scan), babo's are **O(1)**. The gap widens with book depth, so it grows sharply
> at the canonical 1M count.

**Note:** `static` at 1M is slow for liquibook *by design* (that's the O(n)
collapse). With `--reps 3` you pay that 3×; use `--reps 1` for a quick first pass.

---

## What the scripts do **not** run (and why)

The compare script runs **`--mode perf` only** — throughput + correctness. It
deliberately does **not** run audit or latency, because neither is a
per-scenario throughput number you tabulate:

### Audit (anti-cheat)

A separate **pass/fail certification**, run once per engine — not a speed metric.
An engine could hardcode or replay the report stream to reproduce the published
hash, but it can't answer *live* best-bid / best-ask / depth-at-price queries
about a book it never actually maintained. So `--mode audit` replays the workload
through a **trusted public baseline** and compares that baseline's query answers,
at 64 **unpredictable** probe indices, to the answers the engine-under-test gave
during its own run. Both modes probe `engine_query_*` at the same points, so an
engine can't tell a measured run from an audited one.

The baseline is auto-selected: **babo audits against liquibook**; liquibook would
audit against **quantcup** (build it via `scripts/build_baselines.sh` — not built
by default). Run it manually per engine:

```bash
cd cmake-build-release/benchmark
./harness --engine ./babobook_adapter.so --scenario normal --mode audit
```

A `perf` run is `VALID` on correctness alone; an `audit` run additionally
requires the state audit to `PASS`. It's not scripted because you run it once to
certify an engine is honest, not five times to fill a table.

### Latency

The harness has **no latency mode** — its speed metric is throughput, and for a
single-threaded engine **median latency ≈ 1 / throughput** (e.g. 4.66 M msgs/s ⇒
~215 ns/message). The report-stream transport also perturbs per-message timing,
so a clean tail-latency histogram is measured **separately**, by the standalone,
core-pinned micro-benchmarks in **`perf/`**:

| Executable | Measures |
|---|---|
| `babo_latency` / `liqui_latency` | per-operation latency (ns), with percentiles |
| `babo_perf` / `liqui_perf` | long deterministic replay suitable for hardware-counter profiling; also prints raw throughput |

These link the engine **directly** (no adapter / no shared-lib boundary) and pin
to an isolated core, so the numbers are per-order latencies rather than harness
throughput. Build and run them from the `perf/` targets:

```bash
cmake --build cmake-build-release --target babo_latency liqui_latency
./cmake-build-release/perf/babo_latency
```

---

## TL;DR

1. **Build** Release (`harness generator liquibook_adapter babobook_adapter`).
2. **`regen_references`** `--bench-dir <build>/benchmark` — references + babo verification.
3. **`compare_engines`** `--engine adapters/babobook_adapter.<ext> --bench-dir <build>/benchmark` — throughput table + CSV + Markdown.
4. **Audit** (manual, once per engine): `harness --engine ./babobook_adapter.<ext> --mode audit`.
5. **Latency / hardware counters** (separate binaries): `perf/babo_latency`, `perf/babo_perf`, `perf/liqui_latency`, `perf/liqui_perf`.

Steps 2–3 are fully automated by the two scripts; steps 4–5 are the intentionally
separate certification and micro-latency passes.
