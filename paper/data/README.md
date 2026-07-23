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
2. Build (Release) and run both bundles. See `scripts/README.md` for the runner
   commands and `perf/README.md` for the measurement and machine-isolation
   methodology:
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

- Use the same declared boost/SMT/isolation policy for both engines.
- Plug in AC power, disable low-power mode, close heavy applications, and keep
  thermal conditions stable.
- The binaries request CPU affinity and real-time priority automatically; the
  result output records whether those requests succeeded.
- Boost control, Linux boot-time isolation, and SMT control are manual optional
  steps documented in `perf/README.md`.
- macOS affinity is a scheduler hint, not binding to a numbered CPU. Record
  macOS/Apple Silicon runs as unpinned when interpreting cross-machine data.
- Numbers here come from the **core-pinned perf binaries** (direct-linked engine,
  no adapter/transport). Correctness is certified separately by the **harness**
  (SHA-256 report-stream match + state audit); see the repo README.
