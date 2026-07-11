#!/usr/bin/env python3
"""Aggregate cross-machine benchmark bundles into combined paper tables.

Reads every machine's unzipped result bundle under paper/data/<machine>/ and
emits a single Markdown report (paper/data/AGGREGATE.md) with:

  * a machines manifest (CPU / OS / compiler / git commit / dirty),
  * per-scenario cross-machine speedup tables (throughput matrix), and
  * a cross-machine scaling summary (cancel latency vs book size).

The key cross-machine-comparable figure is the SPEEDUP column (babo / liqui);
absolute M msgs/s and ns/cancel vary by CPU/clock/thermals and are not compared
across machines. This script is stdlib-only (no pandas/numpy).

Expected layout (one folder per machine; folder name is free-form, the label is
read from results.json):

    paper/data/
      <machine-label>/
        market_matrix/   report.md results.csv results.json *.svg ...
        scaling/         report.md results.csv results.json *.svg ...
      aggregate.py
      AGGREGATE.md       <- generated

Usage:  python3 paper/data/aggregate.py
"""
from __future__ import annotations

import csv
import glob
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def geomean(values):
    vals = [v for v in values if v and v > 0]
    if not vals:
        return float("nan")
    return math.exp(sum(math.log(v) for v in vals) / len(vals))


def load_json(path):
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def load_csv(path):
    with open(path, newline="", encoding="utf-8") as fh:
        return list(csv.DictReader(fh))


def discover_machines():
    """Return {machine_dir: {'matrix': .../results.*, 'scaling': ...}} sorted by label."""
    machines = {}
    for kind, sub in (("matrix", "market_matrix"), ("scaling", "scaling")):
        for jpath in glob.glob(os.path.join(HERE, "*", sub, "results.json")):
            mdir = os.path.dirname(os.path.dirname(jpath))
            machines.setdefault(mdir, {})[kind] = {
                "json": jpath,
                "csv": os.path.join(os.path.dirname(jpath), "results.csv"),
            }
    return machines


def machine_label(paths):
    for kind in ("matrix", "scaling"):
        if kind in paths:
            return load_json(paths[kind]["json"]).get("metadata", {}).get("label", "?")
    return "?"


def fmt(x, nd=2):
    try:
        return f"{float(x):.{nd}f}"
    except (TypeError, ValueError):
        return "-"


def manifest_table(machines):
    lines = ["## Machines", "",
             "| Label | CPU | OS | Compiler | Git commit | Dirty | Matrix | Scaling |",
             "|---|---|---|---|---|---|:-:|:-:|"]
    for mdir in sorted(machines, key=lambda d: machine_label(machines[d])):
        paths = machines[mdir]
        meta = load_json(paths.get("matrix", paths.get("scaling"))["json"]).get("metadata", {})
        mc = meta.get("machine", {})
        comp = meta.get("compiler", {})
        git = meta.get("git", {})
        comp_str = comp.get("id", comp.get("cxx_compiler", "")) if isinstance(comp, dict) else str(comp)
        lines.append(
            f"| {meta.get('label','?')} | {mc.get('cpu','?')} | {mc.get('os','?')} | "
            f"{comp_str or '?'} | `{str(git.get('commit',''))[:10]}` | "
            f"{git.get('dirty','?')} | {'✓' if 'matrix' in paths else '·'} | "
            f"{'✓' if 'scaling' in paths else '·'} |")
    lines.append("")
    return "\n".join(lines)


def matrix_tables(machines):
    """Per-scenario cross-machine speedup tables + per-machine geomean summary."""
    # machine_label -> scenario -> num_new -> speedup
    data = {}
    scenarios = []  # preserve discovery order
    counts = set()
    for mdir, paths in machines.items():
        if "matrix" not in paths:
            continue
        label = machine_label(paths)
        rows = load_csv(paths["matrix"]["csv"])
        for r in rows:
            scen = r.get("scenario_display") or r.get("scenario")
            if scen not in scenarios:
                scenarios.append(scen)
            n = int(r["num_new"])
            counts.add(n)
            data.setdefault(label, {}).setdefault(scen, {})[n] = float(r["speedup"])

    if not data:
        return "## Throughput matrix\n\n_No market_matrix bundles found._\n"

    labels = sorted(data)
    counts = sorted(counts)
    out = ["## Throughput matrix — cross-machine speedup (babo / liqui)", "",
           "Peak-of-N throughput ratio per (scenario, NEW-order count). "
           "Absolute rates are not comparable across machines; the speedup is.", ""]

    # Per-machine geomean summary (headline)
    out += ["### Speedup summary (geomean across counts)", "",
            "| Scenario | " + " | ".join(labels) + " |",
            "|---|" + "|".join(["---:"] * len(labels)) + "|"]
    for scen in scenarios:
        cells = [fmt(geomean(list(data[l].get(scen, {}).values())), 2) + "×" for l in labels]
        out.append(f"| {scen} | " + " | ".join(cells) + " |")
    out.append("")

    # Per-scenario detail: rows = count, cols = machine
    for scen in scenarios:
        out += [f"### {scen}", "",
                "| NEW orders | " + " | ".join(labels) + " |",
                "|---:|" + "|".join(["---:"] * len(labels)) + "|"]
        for n in counts:
            cells = []
            present = False
            for l in labels:
                v = data[l].get(scen, {}).get(n)
                if v is None:
                    cells.append("-")
                else:
                    present = True
                    cells.append(fmt(v, 2) + "×")
            if present:
                out.append(f"| {n:,} | " + " | ".join(cells) + " |")
        out.append("")
    return "\n".join(out)


def scaling_tables(machines):
    """Cross-machine scaling: speedup by N, plus flat-vs-linear confirmation."""
    data = {}   # label -> N -> (speedup, babo_ns, liqui_ns)
    sizes = set()
    for mdir, paths in machines.items():
        if "scaling" not in paths:
            continue
        label = machine_label(paths)
        for r in load_csv(paths["scaling"]["csv"]):
            n = int(r["size"])
            sizes.add(n)
            data.setdefault(label, {})[n] = (
                float(r["speedup"]), float(r["babo_ns"]), float(r["liqui_ns"]))
    if not data:
        return "## Scaling curve\n\n_No scaling bundles found._\n"

    labels = sorted(data)
    sizes = sorted(sizes)
    out = ["## Scaling curve — cancel latency vs resting-book size", "",
           "Speedup (liqui ns ÷ babo ns) per resting-book size N. babo is O(1) "
           "(flat); liquibook is O(depth) (climbs). The gap widens with N — the "
           "mechanism, not a single machine's constant.", "",
           "### Cancel speedup by N", "",
           "| Resting N | " + " | ".join(labels) + " |",
           "|---:|" + "|".join(["---:"] * len(labels)) + "|"]
    for n in sizes:
        cells = []
        present = False
        for l in labels:
            entry = data[l].get(n)
            if entry is None:
                cells.append("-")
            else:
                present = True
                cells.append(fmt(entry[0], 1) + "×")
        if present:
            out.append(f"| {n:,} | " + " | ".join(cells) + " |")
    out.append("")

    # Flat-vs-linear confirmation per machine
    out += ["### Shape confirmation (ns/cancel range, smallest→largest N)", "",
            "| Label | babo ns (min→max) | liqui ns (min→max) | max speedup |",
            "|---|---|---|---:|"]
    for l in labels:
        entries = [data[l][n] for n in sizes if n in data[l]]
        b = [e[1] for e in entries]
        q = [e[2] for e in entries]
        best = max(e[0] for e in entries)
        out.append(f"| {l} | {fmt(min(b),1)} → {fmt(max(b),1)} | "
                   f"{fmt(min(q),0)} → {fmt(max(q),0)} | {fmt(best,1)}× |")
    out.append("")
    return "\n".join(out)


def main():
    machines = discover_machines()
    report = ["<!-- GENERATED by paper/data/aggregate.py; do not hand-edit. -->",
              "# Cross-machine benchmark aggregate", "",
              f"Machines with data: **{len(machines)}**.", ""]
    report.append(manifest_table(machines))
    report.append(matrix_tables(machines))
    report.append(scaling_tables(machines))
    out_path = os.path.join(HERE, "AGGREGATE.md")
    with open(out_path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(report) + "\n")
    print(f"Wrote {out_path}  ({len(machines)} machine(s))")


if __name__ == "__main__":
    main()
