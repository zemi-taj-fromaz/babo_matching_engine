# Cross-machine benchmark data

This folder collects benchmark result bundles from multiple machines so the
paper can report the **speedup distribution across microarchitectures**, not a
single machine's number. Absolute throughput (`M msgs/s`) and cancel latency
(`ns/cancel`) vary by CPU / clock / thermals; the **speedup** (babo ÷ liquibook)
is the cross-machine-comparable figure.

## Layout

```
paper/data/
  <machine-label>/
    market_matrix/   # unzipped run_market_matrix bundle (results.csv/json, report.md, *.svg)
    scaling/         # unzipped run_scaling bundle
  aggregate.py       # merges every machine into AGGREGATE.md (stdlib-only)
  AGGREGATE.md       # generated — the combined cross-machine tables
```

The folder name is free-form; the real machine label (`OS-CPU`) is read from
each bundle's `results.json` metadata. Commit the **unzipped** bundle contents,
not the `.zip` (the `.zip` is gitignored — it duplicates the folder).

## Contributing a run (for collaborators)

So the data is comparable, every machine must run the **same commit** with the
**same defaults**:

1. Check out the tagged data commit and confirm it's clean:
   ```bash
   git checkout <paper-data-tag>
   git status            # must be clean; bundles record dirty=true otherwise
   ```
2. Build (Release) and run both bundles — see `UPUTE_ZA_PRIJATELJA.txt` in the
   repo root for the plug-in-the-charger / close-heavy-apps checklist:
   ```bash
   cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release
   cmake --build cmake-build-release --target babo_perf liqui_perf babo_scaling liqui_scaling -j
   python3 scripts/run_market_matrix.py --build-dir cmake-build-release
   python3 scripts/run_scaling.py       --build-dir cmake-build-release
   ```
3. Send the two ZIPs from `cmake-build-release/perf/results/`. They are unzipped
   into `paper/data/<label>/{market_matrix,scaling}/` and committed here.

Do **not** tweak `--counts` / `--reps` / `--scenarios`; mismatched cells can't be
compared across machines.

## Regenerating the aggregate

```bash
python3 paper/data/aggregate.py     # writes paper/data/AGGREGATE.md
```

## Measurement-condition notes

- **Disable CPU boost before measuring (fixed base clock).** Base-to-boost is
  ~2× on these laptop parts (e.g. 2.0 vs 4.5 GHz), and boost cannot be sustained
  across a multi-minute sweep, so an unlocked core reports the *same* workload at
  wildly different rates (≈8 vs ≈17 M msgs/s) depending only on thermal state.
  Fix the clock at base so every cell — and every machine — is comparable:
  - **Windows:** `powercfg /setacvalueindex SCHEME_CURRENT SUB_PROCESSOR PERFBOOSTMODE 0`
    then `powercfg /setactive SCHEME_CURRENT` (restore with `PERFBOOSTMODE 2`).
    Note: on AMD, `PROCTHROTTLEMAX 99` alone does **not** disable boost.
  - **Linux:** `echo 0 | sudo tee /sys/devices/system/cpu/cpufreq/boost`
    (Intel pstate: `echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo`).
  - **Apple Silicon:** no boost-lock or core-pin control — measure at stock and
    flag the bundle as unpinned/unlocked.

  Verify it took: a repeated `babo_perf --scenario normal --reps 20` should read
  flat (no 2× jumps). Plug in AC and close heavy apps regardless.
- **Apple Silicon** runs cannot pin to a core (affinity is an ignored hint), so
  those bundles print `could not pin to core 5`. Valid data points — a different,
  *harder* measurement condition — but flag them as unpinned when interpreting.
- Numbers here come from the **core-pinned perf binaries** (direct-linked engine,
  no adapter/transport). Correctness is certified separately by the **harness**
  (SHA-256 report-stream match + state audit); see the repo README.
