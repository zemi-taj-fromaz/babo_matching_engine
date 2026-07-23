# Babobook

**A cache-aware C++20 limit-order book, evaluated against the established
[Liquibook](https://github.com/enewhuis/liquibook) engine through a
correctness-first, cross-platform benchmark.**

Babobook accepts buy and sell orders, preserves price-time priority, matches
trades, cancels and modifies resting orders, handles IOC/AON/stop instructions,
and exposes aggregate market depth. I built it to see whether stable,
cache-local storage could improve cancellation and priority traversal in a
single-threaded matching engine.

The comparison is not against a toy implementation. Liquibook is a mature,
header-only open-source C++ matching engine with approximately **1,500 GitHub
stars and 436 forks** as of July 2026. It represents the conventional
price-level-container design used as the project's independent baseline.

In that design, cancelling an arbitrary order can require scanning the orders
resting at its price. Babobook instead maintains an order-ID index that reaches
the order's exact stable PIN slot. A normal cancellation therefore avoids the
depth-dependent scan; only removal of a newly empty price level returns to the
ordered price tree.

The project is more than an order-book implementation. It includes:

- a header-only matching engine;
- a vendored Liquibook baseline;
- a public C ABI that can host other matching engines;
- deterministic market workloads;
- full report-stream SHA-256 verification;
- an anti-cheat live-state audit;
- direct-link throughput and cancel-scaling benchmarks; and
- reproducible result bundles from Windows, Linux, and macOS.

## Results

The checked-in dataset contains four machines across three operating systems.
On each host, both engines were built together from the recorded clean revision
and measured back-to-back under the same workload and compiler configuration.

| Evidence | Babobook result |
|---|---:|
| Four dynamic scenarios, 10 workload sizes each | **1.97×–4.65×** geomean speedup per scenario and machine |
| Largest `normal` workload (2M NEW orders) | **3.22×–5.81×** faster across all four machines |
| Cancel at 200k resting orders | **74.1×–158.6×** faster |
| Correctness | Complete report stream compared by SHA-256, plus live state audit |

The dynamic-market range is the geometric mean across ten workload sizes for
each scenario/machine pair. The cancel experiment is deliberately diagnostic:
it increases resting depth while timing only cancellation, exposing direct
ID-to-slot cancellation versus Liquibook's per-price `find_on_market` scan.

Full tables, hardware/compiler metadata, and methodology:

- [Cross-machine result aggregate](paper/data/AGGREGATE.md)
- [Performance methodology](perf/README.md)
- [Collected machine bundles](paper/data/README.md)

All controllable Windows and Linux hosts were measured with CPU boost disabled.
The Apple M4 Pro used stock macOS clock management because Apple Silicon exposes
no supported boost lock or numbered-core binding. Absolute rates are
machine-specific; the same-machine Babobook/Liquibook ratio is the principal
comparison.

## Origin and attribution

Babobook follows the theoretical direction of J. Yoon's
[“The World's Fastest Matching Engine Algorithm”](https://arxiv.org/abs/2606.01183)
(arXiv:2606.01183v3, 2026), particularly its Priority-Indicated Node (PIN) and
neighbor-aware ordered-tree ideas.

The order-book core, tree, PIN storage, memory pools, order lifecycle, and
pull-based depth implementation in `libs/babobook/` were written independently
for this project. This repository does not claim the underlying theory as
original work.

The C-ABI contract, deterministic workload model, and benchmark methodology
were adapted from Flash One Technologies'
[Matching Engine Algorithm Performance Challenge](https://github.com/flash1-dev/matching-engine-benchmark),
released under the MIT License. Liquibook is a separately licensed vendored
baseline. See [third-party notices](THIRD_PARTY_NOTICES.md) for provenance.

## How it works

```text
order ID
   │
   ▼
hash index ───────────────► { PIN node, stable slot }
                                  │
price ─► threaded RB tree ─► price-level descriptor
                                  │
                                  └─ boundaries in the global per-side PIN chain
```

- **Direct cancellation:** the ID index reaches the order's PIN and slot without
  walking the price-level queue.
- **Stable inline storage:** fixed-capacity PIN nodes store orders by value;
  erase does not move neighboring payloads.
- **Ordered prices:** a threaded red-black tree locates price levels and supports
  best-first traversal.
- **Global priority chain:** each book side has one ordered PIN chain spanning
  its price levels.
- **Pull-based depth:** top-of-book depth is derived on request instead of
  maintaining a second ordered structure on every message.
- **Pool reuse:** PIN nodes and price-level descriptors return to reusable
  process-wide pools.

“O(1) cancel” means average hash lookup plus constant-time slot unlink within a
populated level. Cancelling the final order at a price additionally removes that
RB-tree level in O(log P).

## Correctness before speed

The project intentionally separates certification from performance measurement.

### `benchmark/`: public ABI and correctness

The harness dynamically loads each engine adapter through
[`matching_engine_api.h`](benchmark/api/matching_engine_api.h), replays identical
messages, drains every acknowledgement/trade/cancel/modify report, and compares
the complete report stream with a trusted SHA-256 reference.

Audit mode also queries live BBO, depth-at-price, and order state at unpredictable
replay positions. This makes it harder to “win” by emitting a memorized report
stream without maintaining the book.

### `perf/`: matching-path performance

The perf executables link each engine directly, use no-op listeners, perform one
warmup, request CPU affinity and elevated priority, and report peak, median, and
mean throughput. The scaling executables separately time cancellation against
resting-book size.

Perf results do not prove correctness; harness certification is the prerequisite
for trusting them. Exact timed boundaries, CPU-isolation behavior, boost/SMT
controls, and limitations are documented in [`perf/README.md`](perf/README.md).

## Quick start

Prerequisites: CMake 3.23+, a C++20 compiler, Python 3 for result runners, and
internet access on the first configure for GoogleTest and Rigtorp SPSCQueue.

Choose any build directory; `build/release` is only an example selected here:

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release -j
ctest --test-dir build/release --output-on-failure
```

Run the integration example:

```bash
./build/release/libs/babobook/examples/babo_basic_book
```

Run one direct performance comparison:

```bash
./build/release/perf/babo_perf  --scenario normal --reps 10
./build/release/perf/liqui_perf --scenario normal --reps 10
```

## Reproduce the evidence

Primary throughput matrix:

```bash
bash scripts/run_market_matrix.sh --build-dir build/release
```

Cancel-scaling curve:

```bash
bash scripts/run_scaling.sh --build-dir build/release
```

Windows PowerShell equivalents:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run_market_matrix.ps1 `
  -BuildDir build\release

powershell -ExecutionPolicy Bypass -File scripts\run_scaling.ps1 `
  -BuildDir build\release
```

The runners produce Markdown, CSV, JSON, SVG, raw output, binary hashes,
environment metadata, a manifest, and a shareable ZIP under
`<build-dir>/perf/results/`.

Correctness/reference generation and arbitrary-adapter comparison are described
in [`scripts/README.md`](scripts/README.md).

## Use Babobook as a header-only library

```cmake
add_subdirectory(path/to/babo_matching_engine/libs/babobook)
target_link_libraries(your_target PRIVATE babobook::babobook)
```

```cpp
#include <book/matching_book.h>
#include <book/simple_order.h>

babo::book::matching_book<5> book;
```

The always-built [`basic_book.cpp`](libs/babobook/examples/basic_book.cpp)
example demonstrates listeners, add, match, cancel, and depth.

Babobook is a single-threaded matching core. Its process-wide pools are
unsynchronized, so book construction, operations, and destruction must be
serialized on one matching thread. Books must have scoped, non-static lifetime.

## Bring another engine

Any engine can participate by implementing the C functions in
[`matching_engine_api.h`](benchmark/api/matching_engine_api.h) and building a
shared-library adapter. The boundary carries only fixed-layout C structs; no C++
types cross between harness and engine.

Start with
[`template_adapter.h.cpp`](benchmark/adapters/template_adapter.h.cpp), then use
`compare_engines` and audit mode to certify the adapter under the same contract.

## Repository map

| Path | Purpose |
|---|---|
| [`libs/babobook/`](libs/babobook/) | Header-only engine and integration example |
| [`libs/liquibook/`](libs/liquibook/) | Vendored reference engine |
| [`benchmark/`](benchmark/) | C-ABI harness, workload generator, correctness, and audit |
| [`perf/`](perf/) | Direct-link throughput and cancel-scaling methodology |
| [`scripts/`](scripts/) | Supported cross-platform result workflows |
| [`test/`](test/) | Babobook and adapted Liquibook GoogleTest suites |
| [`paper/data/`](paper/data/) | Cross-machine bundles and generated aggregate |
| [`paper/`](paper/) | Engineering-report source and figures (draft) |

## Scope

This is a systems-performance and data-structure comparison, not a complete
exchange. Networking, persistence, recovery, risk checks, regulatory controls,
and multi-symbol scheduling are outside the measured system. The synthetic
workload is deterministic and calibrated, but it is not raw market-by-order
traffic. Results apply to the checked-in engines, workload, compilers, and
hardware—not to every conventional order-book design.
