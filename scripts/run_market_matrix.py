#!/usr/bin/env python3
"""Throughput matrix: babo vs liquibook across market scenarios x message-scale.

For every (scenario, message-count) cell it replays the deterministic workload
through the core-pinned perf binaries `--reps` times and records the PEAK (best
rep) throughput — the stable estimator (see PEAK/median in the perf output). The
result is one paper-ready bundle: a per-scenario table + line chart of throughput
vs. message count for both engines, a combined speedup chart, plus CSV/JSON and
full CPU/compiler/git metadata.

    python scripts/run_market_matrix.py --build-dir cmake-build-release --label mypc

Default matrix: scenarios {normal, swing25, flash_crash_40, flash_crash_60}
(static is skipped — its O(n) liquibook collapse dominates runtime and is the
separate scaling experiment), counts 1k..2M, best of 10 reps. ~a few minutes.
"""
from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import math
import os
import platform
import re
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_portable_perf as rpp  # noqa: E402  (reuse env/cpu/git/sha helpers)

DEFAULT_SCENARIOS = ["normal", "swing25", "flash_crash_40", "flash_crash_60"]
DISPLAY = {"static": "Static", "normal": "Normal", "swing25": "Swing-25",
           "flash_crash_40": "Swing-40", "flash_crash_60": "Flash-crash-60"}
DEFAULT_COUNTS = [1_000, 5_000, 10_000, 20_000, 50_000,
                  100_000, 200_000, 500_000, 1_000_000, 2_000_000]
ENGINES = [("babobook", "babo_perf"), ("liquibook", "liqui_perf")]
BLUE, ORANGE = "#1769aa", "#d95f02"
SERIES_COLORS = ["#1769aa", "#d95f02", "#1b9e77", "#7570b3", "#e7298a"]

def default_label() -> str:
    """Descriptive machine tag from OS + CPU model (not the arbitrary hostname)."""
    cpu = rpp.cpu_name()
    cpu = re.sub(r"\(R\)|\(TM\)|\(r\)|\(tm\)|®|™", "", cpu)
    cpu = re.sub(r"\bwith Radeon Graphics\b", "", cpu, flags=re.IGNORECASE)
    cpu = re.sub(r"\b(CPU|Processor)\b", "", cpu, flags=re.IGNORECASE)
    cpu = re.sub(r"@.*$", "", cpu)
    cpu = re.sub(r"\s+", " ", cpu).strip()
    return f"{platform.system()}-{cpu}" if cpu else (platform.system() or "unknown")


PERFROW_RE = re.compile(
    r"PERFROW\s+engine=(\S+)\s+scenario=(\S+)\s+num_new=(\d+)\s+messages=(\d+)\s+"
    r"reps=(\d+)\s+peak_mps=([\d.]+)\s+median_mps=([\d.]+)\s+mean_mps=([\d.]+)\s+resting=(\d+)")


def run_cell(binary: Path, scenario: str, count: int, reps: int) -> dict:
    cmd = [str(binary), "--scenario", scenario, "--count", str(count), "--reps", str(reps)]
    rc, out = rpp.run_text(cmd, cwd=binary.parent)
    if rc != 0:
        raise RuntimeError(f"{binary.name} {scenario}@{count} failed ({rc}):\n{out}")
    m = PERFROW_RE.search(rpp.ANSI_RE.sub("", out))
    if not m:
        raise RuntimeError(f"no PERFROW for {binary.name} {scenario}@{count}:\n{out}")
    return {"scenario": m.group(2), "num_new": int(m.group(3)), "messages": int(m.group(4)),
            "reps": int(m.group(5)), "peak_mps": float(m.group(6)),
            "median_mps": float(m.group(7)), "mean_mps": float(m.group(8))}


def line_chart(path: Path, title: str, xlabel: str, ylabel: str,
               series: list[dict], y_log: bool = False) -> None:
    """Generic dependency-free line chart. Each series: {label,color,points=[(x,y)]}. x is log."""
    width, height = 900, 520
    left, right, top, bottom = 85, 175, 52, 74
    pw, ph = width - left - right, height - top - bottom
    xs = [x for s in series for x, _ in s["points"]]
    ys = [y for s in series for _, y in s["points"]]
    if not xs:
        return
    xlo, xhi = math.log10(min(xs)), math.log10(max(xs))
    if xhi == xlo:
        xhi = xlo + 1
    if y_log:
        ylo, yhi = math.log10(max(1e-9, min(ys))), math.log10(max(ys))
    else:
        ylo, yhi = 0.0, max(ys) * 1.1 or 1.0
    if yhi == ylo:
        yhi = ylo + 1

    def px(x):
        return left + (math.log10(x) - xlo) / (xhi - xlo) * pw

    def py(y):
        v = (math.log10(max(1e-9, y)) if y_log else y)
        return top + ph - (v - ylo) / (yhi - ylo) * ph

    s = ["<svg xmlns='http://www.w3.org/2000/svg' width='%d' height='%d' viewBox='0 0 %d %d'>"
         % (width, height, width, height),
         "<rect width='100%' height='100%' fill='white'/>",
         "<style>text{font-family:Arial,sans-serif;fill:#222}.grid{stroke:#e8e8e8}.axis{stroke:#333}</style>",
         "<text x='%d' y='28' text-anchor='middle' font-size='17' font-weight='bold'>%s</text>"
         % (width // 2, title)]
    for i in range(6):
        if y_log:
            e = ylo + (yhi - ylo) * i / 5
            val, yy = 10 ** e, py(10 ** e)
            lab = f"{val:,.0f}" if val >= 1 else f"{val:.2g}"
        else:
            val = yhi * i / 5
            yy = py(val)
            lab = f"{val:.1f}"
        s.append("<line class='grid' x1='%d' y1='%.1f' x2='%d' y2='%.1f'/>" % (left, yy, left + pw, yy))
        s.append("<text x='%d' y='%.1f' text-anchor='end' font-size='11'>%s</text>" % (left - 8, yy + 4, lab))
    s.append("<line class='axis' x1='%d' y1='%d' x2='%d' y2='%d'/>" % (left, top, left, top + ph))
    s.append("<line class='axis' x1='%d' y1='%d' x2='%d' y2='%d'/>" % (left, top + ph, left + pw, top + ph))
    for x in sorted(set(xs)):
        lab = (f"{x//1_000_000}M" if x >= 1_000_000 else f"{x//1000}K" if x >= 1000 else str(x))
        s.append("<text x='%.1f' y='%d' text-anchor='middle' font-size='10'>%s</text>" % (px(x), top + ph + 20, lab))
    for si, ser in enumerate(series):
        pts = " ".join("%.1f,%.1f" % (px(x), py(y)) for x, y in ser["points"])
        s.append("<polyline fill='none' stroke='%s' stroke-width='3' points='%s'/>" % (ser["color"], pts))
        for x, y in ser["points"]:
            s.append("<circle cx='%.1f' cy='%.1f' r='4' fill='%s'/>" % (px(x), py(y), ser["color"]))
        ly = top + 8 + si * 20
        s.append("<line x1='%d' y1='%d' x2='%d' y2='%d' stroke='%s' stroke-width='3'/>"
                 "<text x='%d' y='%d' font-size='12'>%s</text>"
                 % (left + pw + 14, ly, left + pw + 40, ly, ser["color"], left + pw + 46, ly + 4, ser["label"]))
    s.append("<text transform='translate(20 %d) rotate(-90)' text-anchor='middle' font-size='13'>%s</text>"
             % (top + ph // 2, ylabel))
    s.append("<text x='%d' y='%d' text-anchor='middle' font-size='13'>%s</text>" % (left + pw // 2, height - 12, xlabel))
    s.append("</svg>")
    path.write_text("\n".join(s), encoding="utf-8")


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--scenarios", default=",".join(DEFAULT_SCENARIOS))
    ap.add_argument("--counts", default=",".join(str(c) for c in DEFAULT_COUNTS))
    ap.add_argument("--reps", type=int, default=10)
    ap.add_argument("--metric", choices=["peak", "median", "mean"], default="peak",
                    help="which per-cell throughput to report: peak=best rep, "
                         "median=middle rep, mean=aggregate over all reps (default peak)")
    ap.add_argument("--label", default="")
    ap.add_argument("--output-dir", default="")
    args = ap.parse_args()

    scenarios = [x.strip() for x in args.scenarios.split(",") if x.strip()]
    counts = sorted({int(x) for x in args.counts.split(",") if x.strip()})
    if args.reps < 1 or not scenarios or not counts:
        ap.error("need reps>=1, at least one scenario and one count")
    label = args.label or default_label()

    script = Path(__file__).resolve()
    repo = script.parent.parent
    build_dir = Path(args.build_dir).resolve()
    perf_dir = build_dir / "perf" if (build_dir / "perf").is_dir() else build_dir
    bins = {name: rpp.find_binary(build_dir, exe) for name, exe in ENGINES}

    parent = Path(args.output_dir).resolve() if args.output_dir else perf_dir / "results"
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    bundle = parent / f"market_matrix_{stamp}"
    bundle.mkdir(parents=True, exist_ok=False)
    metric_key = f"{args.metric}_mps"

    total = len(scenarios) * len(counts) * len(ENGINES)
    done = 0
    # data[scenario][count][engine] = cell dict.
    # Engines are measured innermost (babo then liquibook, back-to-back) so both
    # share the same thermal window for each cell — the speedup ratio must not
    # depend on how much the CPU drifted between an all-babo and an all-liqui pass.
    data: dict = {s: {c: {} for c in counts} for s in scenarios}
    for scenario in scenarios:
        for count in counts:
            for engine, _ in ENGINES:
                done += 1
                print(f"  [{done}/{total}] {engine:9s} {DISPLAY.get(scenario, scenario):14s} count={count:>9,}",
                      flush=True)
                data[scenario][count][engine] = run_cell(bins[engine], scenario, count, args.reps)

    rows = []
    for scenario in scenarios:
        for count in counts:
            b = data[scenario][count]["babobook"]
            l = data[scenario][count]["liquibook"]
            rows.append({
                "scenario": scenario, "scenario_display": DISPLAY.get(scenario, scenario),
                "num_new": count, "messages": b["messages"], "reps": args.reps,
                "babo_peak_mps": b["peak_mps"], "babo_median_mps": b["median_mps"],
                "liqui_peak_mps": l["peak_mps"], "liqui_median_mps": l["median_mps"],
                "babo_metric": b[metric_key], "liqui_metric": l[metric_key],
                "speedup": (b[metric_key] / l[metric_key]) if l[metric_key] else 0.0,
            })

    # ---- charts -------------------------------------------------------------
    svg_names = {}
    for scenario in scenarios:
        srows = [r for r in rows if r["scenario"] == scenario]
        name = f"throughput_{scenario}.svg"
        svg_names[scenario] = name
        line_chart(bundle / name, f"{DISPLAY.get(scenario, scenario)} — throughput vs message count",
                   "Messages replayed (log)", f"M msgs/s ({args.metric})",
                   [{"label": "babobook", "color": BLUE,
                     "points": [(r["messages"], r["babo_metric"]) for r in srows]},
                    {"label": "liquibook", "color": ORANGE,
                     "points": [(r["messages"], r["liqui_metric"]) for r in srows]}])
    combined_svg = "speedup_by_scenario.svg"
    line_chart(bundle / combined_svg, "babo cancel/throughput speedup vs liquibook",
               "Messages replayed (log)", "Speedup (x)",
               [{"label": DISPLAY.get(s, s), "color": SERIES_COLORS[i % len(SERIES_COLORS)],
                 "points": [(r["messages"], r["speedup"]) for r in rows if r["scenario"] == s]}
                for i, s in enumerate(scenarios)])

    # ---- metadata + outputs -------------------------------------------------
    mem = rpp.memory_bytes()
    meta = {
        "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "generated_by": "scripts/run_market_matrix.py",
        "runner_sha256": rpp.sha256(script), "label": label,
        "scenarios": scenarios, "counts": counts, "reps": args.reps, "metric": args.metric,
        "machine": {"os": platform.platform(), "cpu": rpp.cpu_name(),
                    "logical_cpus": os.cpu_count(),
                    "memory_gib": round(mem / 2**30, 2) if mem else None,
                    "python": platform.python_version()},
        "compiler": rpp.compiler_metadata(build_dir), "git": rpp.git_metadata(repo),
        "binaries": {name: {"sha256": rpp.sha256(p)} for name, p in bins.items()},
    }

    (bundle / "results.json").write_text(json.dumps({"metadata": meta, "results": rows}, indent=2) + "\n",
                                         encoding="utf-8")
    with (bundle / "results.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader(); w.writerows(rows)

    c, g, mc = meta["compiler"], meta["git"], meta["machine"]
    md = ["<!-- GENERATED by scripts/run_market_matrix.py; do not hand-edit. -->",
          "# babobook vs liquibook — throughput across market regimes and scale", "",
          f"- **Label:** {label}",
          f"- **Generated (UTC):** {meta['created_utc']}",
          f"- **CPU / OS:** {mc['cpu']} — {mc['os']}",
          f"- **RAM / logical CPUs:** {mc['memory_gib']} GiB / {mc['logical_cpus']}",
          f"- **Compiler:** {c['cxx_compiler_id']} {c['cxx_compiler_version']} · build `{c['cmake_build_type']}`",
          f"- **Git:** `{g['commit']}` (branch `{g['branch']}`, dirty `{g['dirty']}`)",
          f"- **Protocol:** core-pinned perf binaries, no-op listener; {args.reps} reps per cell, "
          f"reporting **{args.metric}** ({ {'peak': 'per-rep min / best', 'median': 'per-rep median', 'mean': 'aggregate mean over all reps'}[args.metric] }); 1 warmup per cell.",
          f"- **Scale:** {', '.join(f'{c:,}' for c in counts)} NEW orders (messages ≈ 2.25×).", "",
          f"![speedup]({combined_svg})", ""]
    for scenario in scenarios:
        srows = [r for r in rows if r["scenario"] == scenario]
        md += [f"## {DISPLAY.get(scenario, scenario)}", "",
               f"![throughput]({svg_names[scenario]})", "",
               "| NEW orders | Messages | babobook M/s | liquibook M/s | Speedup |",
               "|---:|---:|---:|---:|---:|"]
        for r in srows:
            md.append(f"| {r['num_new']:,} | {r['messages']:,} | {r['babo_metric']:.2f} | "
                      f"{r['liqui_metric']:.2f} | {r['speedup']:.2f}× |")
        md.append("")
    md += ["> `M msgs/s` is the %s of %d reps (matching-core throughput, no report emission). "
           "Absolute rates vary by CPU/clock; the **speedup** column is the cross-machine-comparable figure."
           % (args.metric, args.reps)]
    (bundle / "report.md").write_text("\n".join(md) + "\n", encoding="utf-8")

    artifacts = sorted(p for p in bundle.iterdir() if p.is_file() and p.name != "MANIFEST.sha256")
    (bundle / "MANIFEST.sha256").write_text(
        "".join(f"{rpp.sha256(p)}  {p.name}\n" for p in artifacts), encoding="ascii")
    archive = Path(shutil.make_archive(str(bundle), "zip", root_dir=bundle))
    print(f"\nResult bundle: {bundle}\nShareable ZIP:  {archive}\n")
    print((bundle / "report.md").read_text(encoding="utf-8"))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
