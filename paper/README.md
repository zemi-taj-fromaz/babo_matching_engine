# Paper

`paper.tex` is the compact, two-column engineering report for this project.
It is intentionally written for technical hiring managers: the first page states
the result and contribution, the middle explains the implementation and evidence,
and the final sections make the benchmark reproducible and extensible.

## Draft status

The prose, implementation description, workload methodology, correctness model,
limitations, and reproduction flow are drafted. Red `[FINAL MEASUREMENT: ...]`
markers remain where the following evidence must be inserted after a clean build:

- author name and repository URL;
- final hardware/compiler/build metadata;
- final five-scenario throughput table;
- run-to-run dispersion;
- resting-book-size scaling curve;
- depth on/off comparison;
- optional PIN-capacity sweep.

The document explicitly labels the saved July 7 harness numbers as pre-redesign
development evidence. They must not be used as the final 4x headline result.

## Build

Use any standard LaTeX distribution:

```bash
pdflatex paper.tex
pdflatex paper.tex
```

Run the command from this directory. The second pass resolves references.

The source uses the standard `article` class in two-column mode with a 9.5-point
body font, narrow margins, compact tables, and no conference-specific template.

## Payload-scaling graph

From the repository root on Windows:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\compare_payload_scaling.ps1 `
  -Engine adapters\babobook_adapter.dll `
  -BenchDir cmake-build-release\benchmark
```

This produces CSV, Markdown, and SVG files under the build's
`benchmark/results/` directory for 1K, 10K, 100K, 1M, and 10M generated NEW
orders. Add `-Include100M` only on a machine with sufficient disk and tens of
gigabytes of available memory. Input-payload size is not the same as resting-book
size; the paper treats those as separate experiments.
