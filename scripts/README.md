# Scripts

Build the project in `Release` mode before running these commands. Replace
`<build-dir>` everywhere below with the directory passed to CMake's `-B` option:

```bash
cmake -S . -B <build-dir> -DCMAKE_BUILD_TYPE=Release
cmake --build <build-dir> -j
```

There are four workflows.

| Script | Purpose |
|---|---|
| `regen_references` | Generate trusted correctness hashes with Liquibook, then verify Babobook against them. |
| `compare_engines` | Check an engine adapter through the public C ABI and compare it with Liquibook. |
| `run_market_matrix` | Measure matching-core throughput across market scenarios and workload sizes. |
| `run_scaling` | Measure cancel latency as the resting book grows. |

## 1. Regenerate correctness references

Run this only when the workload or expected matching behavior intentionally changes. It updates the tracked file `benchmark/reference/correctness_hash.txt`.

macOS/Linux:

```bash
bash scripts/regen_references.sh --bench-dir <build-dir>/benchmark
```

Windows:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\regen_references.ps1 `
  -BenchDir <build-dir>\benchmark
```

## 2. Check an engine adapter

This runs all market scenarios through the benchmark harness. It checks correctness and reports end-to-end throughput. Use `run_market_matrix` for the primary matching-core performance measurement.

macOS:

```bash
bash scripts/compare_engines.sh \
  --engine adapters/babobook_adapter.dylib \
  --bench-dir <build-dir>/benchmark
```

Linux: use `babobook_adapter.so` instead of `.dylib`.

Windows:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\compare_engines.ps1 `
  -Engine adapters\babobook_adapter.dll `
  -BenchDir <build-dir>\benchmark
```

## 3. Run the throughput matrix

This is the primary throughput test. It runs the directly linked `babo_perf` and
`liqui_perf` binaries and creates Markdown, CSV, JSON, SVG charts, and a ZIP
under `<build-dir>/perf/results/`.

macOS/Linux:

```bash
bash scripts/run_market_matrix.sh --build-dir <build-dir>
```

Windows:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run_market_matrix.ps1 `
  -BuildDir <build-dir>
```

## 4. Run cancel scaling

This measures cancel latency against resting-book size and creates the same type of result bundle.

macOS/Linux:

```bash
bash scripts/run_scaling.sh --build-dir <build-dir>
```

Windows:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run_scaling.ps1 `
  -BuildDir <build-dir>
```

The two performance runners require Python 3. The `.sh` and `.ps1` files are only platform-friendly launchers; the `.py` files contain the shared cross-platform implementation.
