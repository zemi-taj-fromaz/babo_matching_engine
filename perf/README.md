# Performance benchmarks

`perf/` measures the engines directly. It is separate from `benchmark/`, which
certifies correctness through the public C ABI, report-stream hashes, and the
state audit.

Use both:

1. Use `benchmark/` to prove that an adapter is correct.
2. Use `perf/` to measure and profile the engine implementation.

The performance binaries do **not** perform correctness certification.

## Where this code came from

The benchmark harness under `benchmark/` is vendored MIT-licensed code. The
`perf/` directory is project-local code built around the two engines, but its
workload methodology was borrowed from that harness:

- `workload.hpp` is a header-only adaptation of
  `benchmark/workload/generator.cpp`.
- `workload_reader.h` reads the harness generator's `workload.bin` format.
- Both engine binaries replay the same message model and scenario parameters.
- `bench_util.h`, `bench_log.h`, the engine-specific runners, and the scaling
  experiment are local support code, not part of the vendored harness.

The adapted generator intentionally follows the harness model: deterministic
`std::mt19937` randomness, GBM mid-price movement, power-law price depth,
cancel/modify/IOC lifecycles, stale duplicate requests, and shuffled arrival
order. Its default seed is `23`.

`workload.hpp` is still a second implementation of the generator. If exact
identity with a harness run matters, generate one canonical `.bin` file with
`benchmark/generator` and pass that same file to both perf binaries.

## Built binaries

| Binary | What it measures |
|---|---|
| `babo_perf` | Babobook throughput across market scenarios |
| `liqui_perf` | Liquibook throughput across the same scenarios |
| `babo_scaling` | Babobook cancel latency as the resting book grows |
| `liqui_scaling` | Liquibook cancel latency under the same scaling protocol |

All four are built by default:

```bash
cmake --build <build-dir> --target \
  babo_perf liqui_perf babo_scaling liqui_scaling
```

The optional `-DBABO_BUILD_PIN_SWEEP=ON` configure flag also builds
`babo_cap16_perf`, `babo_cap32_perf`, and `babo_cap128_perf`. The canonical
`babo_perf` uses PIN capacity 64.

## Throughput methodology

`babo_perf` and `liqui_perf`:

1. Generate a workload before timing, or load a supplied `.bin`.
2. Run one complete warmup replay.
3. Run every measured repetition on a fresh order book.
4. Time each repetition with `std::chrono::steady_clock`.
5. Report the fastest repetition (`PEAK`), median repetition, and aggregate
   mean throughput.

The workload is measured in delivered messages, not only NEW orders. A workload
also contains cancel and modify messages.

The timer excludes:

- workload generation or `.bin` loading;
- adapter/shared-library dispatch;
- the harness SPSC report transport;
- report serialization, SHA-256 verification, and the state audit;
- console and result-bundle generation.

The timer includes:

- construction and destruction of a fresh book for every repetition;
- NEW, CANCEL, and MODIFY dispatch;
- the no-op listener callbacks;
- engine-side order allocation and deallocation;
- the final resting-order traversal used as an optimization barrier.

Therefore these binaries measure the directly linked engine replay path, not
just one isolated tree operation. They remove the public harness pipeline while
retaining the engine's normal per-replay lifetime costs.

Both canonical books use aggregate depth support. Babobook depth is pull-based,
so `depth()` is not called during the replay; Liquibook maintains its normal
depth state. The benchmark measures order processing, not the cost of requesting
a Babobook depth snapshot.

### Metrics

- `PEAK`: throughput of the fastest measured repetition. This minimizes
  interference and is the default paper/matrix metric.
- `median`: middle repetition, useful for seeing typical run-to-run behavior.
- `THROUGHPUT`: total messages divided by total measured wall time, effectively
  the mean across repetitions.

All rates are `M msgs/s`. Absolute rates depend on the CPU, compiler, clocks,
thermal state, and operating system. The Babobook/Liquibook ratio measured
back-to-back on one machine is the more portable comparison.

## Scenarios

The throughput binaries support:

| Scenario | Meaning |
|---|---|
| `static` | No mid-price movement; stresses cancellations in accumulated levels |
| `normal` | Normal-volatility market path |
| `swing25` | Higher-volatility path targeting a 25% swing |
| `flash_crash_40` | Flash-crash path targeting a 40% swing |
| `flash_crash_60` | Flash-crash path targeting a 60% swing |

Example:

```bash
<build-dir>/perf/babo_perf --scenario normal --count 1000000 --reps 10
<build-dir>/perf/liqui_perf --scenario normal --count 1000000 --reps 10
```

To replay the exact same harness-generated file:

```bash
<build-dir>/perf/babo_perf path/to/workload.bin --reps 10
<build-dir>/perf/liqui_perf path/to/workload.bin --reps 10
```

For the complete scenario-by-size comparison and its Markdown/CSV/JSON/SVG/ZIP
bundle, use `scripts/run_market_matrix`.

## Cancel-scaling methodology

The scaling experiment isolates cancellation:

1. Build a fresh book.
2. Prefill `N` non-crossing bid orders outside the timed region.
3. Spread those orders over `L` fixed price levels.
4. Cancel all `N` orders in one deterministic shuffled order.
5. Read the drained state so the compiler cannot remove the loop.
6. Repeat on fresh books and retain the fastest run.

Default sizes are 1,000 through 200,000 orders, with `L = 64` and three measured
repetitions per size. Depth per contended level is approximately `N / L`.

Prefill, order allocation, cleanup, and shuffled-order generation are outside
the cancel timer. The two engines receive identical IDs, prices, quantities,
level distribution, and cancellation order.

Babobook resolves an order ID through its hash index to a stable PIN slot.
Liquibook receives an order pointer but `find_on_market` scans the corresponding
price level to locate it. Consequently, Babobook is expected to remain close to
constant-time while Liquibook grows with the number of orders at a level
(`O(log L + depth)`, dominated by the scan).

Run the binaries directly:

```bash
<build-dir>/perf/babo_scaling --levels 64 --reps 3
<build-dir>/perf/liqui_scaling --levels 64 --reps 3
```

Or use `scripts/run_scaling` to run both and create the shareable result bundle.

## CPU affinity and priority

Every perf binary calls `pin_and_isolate(5)` before warmup. Core `5` is currently
a compile-time constant in the four runner `.cpp` files; there is no `--core`
option.

The implementation is platform-specific:

| Platform | Affinity | Priority |
|---|---|---|
| Linux | `sched_setaffinity` binds the calling thread to logical CPU 5 | `SCHED_FIFO` at maximum priority minus one |
| Windows | `SetThreadAffinityMask` binds the calling thread to logical CPU 5 | `THREAD_PRIORITY_TIME_CRITICAL` |
| macOS | Mach `THREAD_AFFINITY_POLICY` supplies affinity tag 6; it is only a scheduler/cache hint, not a binding to CPU 5 | `pthread_setschedparam` requests maximum `SCHED_FIFO` priority |

Linux real-time priority normally requires root or `CAP_SYS_NICE`; Windows may
require elevated privileges. macOS may reject the real-time request, and Apple
Silicon does not provide a public API for binding this thread to one numbered
performance core. The binary prints whether each request succeeded.

“Raised priority” is only runtime isolation. These programs do not configure
Linux `isolcpus`/`nohz_full`, reserve a Windows CPU set, select an Apple
performance core, stop background services, disable SMT, or disable CPU
turbo/boost. Those are external machine-preparation choices and must be reported
with published results.

## Recommended measurement conditions

- Use a `Release` build and keep the compiler/configuration identical for both
  engines.
- Run both engines on the same machine, preferably back-to-back.
- Close heavy background applications and keep power/thermal conditions stable.
- Treat affinity or priority warnings as metadata, not as silent success.
- Record whether turbo/boost was enabled or disabled.
- Use the scripts so results include OS, CPU, compiler, Git revision, binary
  hashes, raw output, and a manifest.
- Use external tools such as Linux `perf`, AMD uProf, Intel VTune, or platform
  equivalents for hardware counters. The binaries do not collect counters
  themselves.

## Interpretation and limitations

- A no-op listener intentionally removes report-production cost.
- `perf/` does not verify event semantics; passing the harness correctness and
  audit checks is a prerequisite for trusting a performance result.
- `PEAK` represents the least-interfered cache-hot repetition, not tail latency
  or sustained multi-hour throughput.
- The engine is single-threaded; these tests measure one matching thread.
- Cross-machine absolute throughput is not directly comparable.
- The internal generator mirrors vendored code and can drift. A shared `.bin`
  is the strictest way to guarantee identical input bytes.
