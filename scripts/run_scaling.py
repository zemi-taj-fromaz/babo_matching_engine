#!/usr/bin/env python3
"""Run the babo/liquibook cancel-vs-resting-book-size sweep and bundle it.

Produces the O(1)-cancel "money figure": cancel latency (ns/cancel) as the
resting book — and thus the depth per contended price level — grows. babo's
id-indexed cancel stays ~flat (cache only); liquibook's find_on_market rescan
climbs O(depth). Emits CSV, JSON, Markdown, a dependency-free log-log SVG, and a
shareable ZIP with full environment/compiler/git metadata, so results from many
machines can be dropped side by side.

    python scripts/run_scaling.py --build-dir cmake-build-release --label alice-win
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

# Reuse the portable-perf helpers (env/cpu/ram/git/compiler/sha) so there is one
# source of truth for machine metadata.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import _script_utils as utils  # noqa: E402

SCALEROW_RE = re.compile(
    r"SCALEROW\s+engine=(\S+)\s+size=(\d+)\s+levels=(\d+)\s+depth=(\d+)\s+"
    r"cancels=(\d+)\s+seconds=([\d.]+)\s+mps=([\d.]+)\s+ns_per_cancel=([\d.]+)")


def default_label() -> str:
    """A descriptive machine tag from OS + CPU model (not the arbitrary hostname)."""
    cpu = utils.cpu_name()
    cpu = re.sub(r"\(R\)|\(TM\)|\(r\)|\(tm\)|®|™", "", cpu)
    cpu = re.sub(r"\bwith Radeon Graphics\b", "", cpu, flags=re.IGNORECASE)
    cpu = re.sub(r"\b(CPU|Processor)\b", "", cpu, flags=re.IGNORECASE)
    cpu = re.sub(r"@.*$", "", cpu)                # drop "@ 2.00GHz"
    cpu = re.sub(r"\s+", " ", cpu).strip()
    return f"{platform.system()}-{cpu}" if cpu else (platform.system() or "unknown")


def parse_scalerows(text: str, expected_engine: str) -> list[dict]:
    clean = utils.ANSI_RE.sub("", text).replace("\r", "")
    rows = []
    for m in SCALEROW_RE.finditer(clean):
        if m.group(1) != expected_engine:
            raise RuntimeError(f"expected engine {expected_engine}, saw {m.group(1)}")
        rows.append({
            "engine": m.group(1),
            "size": int(m.group(2)),
            "levels": int(m.group(3)),
            "depth": int(m.group(4)),
            "cancels": int(m.group(5)),
            "seconds": float(m.group(6)),
            "mps": float(m.group(7)),
            "ns_per_cancel": float(m.group(8)),
        })
    if not rows:
        raise RuntimeError(f"no SCALEROW lines parsed for {expected_engine}")
    return rows


def run_engine(binary: Path, engine: str, args, bundle: Path) -> list[dict]:
    cmd = [str(binary), "--levels", str(args.levels), "--reps", str(args.reps)]
    if args.sizes:
        cmd += ["--sizes", args.sizes]
    if args.max:
        cmd += ["--max", str(args.max)]
    print(f"Running {engine}: {' '.join(cmd[1:])}", flush=True)
    rc, output = utils.run_text(cmd, cwd=binary.parent)
    (bundle / f"raw_{engine}.txt").write_text(output, encoding="utf-8")
    if rc != 0:
        raise RuntimeError(f"{binary.name} failed ({rc}); see raw_{engine}.txt")
    return parse_scalerows(output, engine)


def log_log_svg(rows: list[dict], path: Path, levels: int) -> None:
    """Hand-rolled log-log SVG (no matplotlib): x = resting size, y = ns/cancel."""
    width, height = 1000, 580
    left, right, top, bottom = 90, 190, 55, 80
    plot_w, plot_h = width - left - right, height - top - bottom
    xs = [r["size"] for r in rows]
    ys = [r["babo_ns"] for r in rows] + [r["liqui_ns"] for r in rows]
    xmin, xmax = math.log10(min(xs)), math.log10(max(xs))
    ymin, ymax = math.log10(max(1.0, min(ys))), math.log10(max(ys))
    if xmax == xmin:
        xmax = xmin + 1
    if ymax == ymin:
        ymax = ymin + 1

    def px(size):
        return left + (math.log10(size) - xmin) / (xmax - xmin) * plot_w

    def py(ns):
        return top + plot_h - (math.log10(max(1.0, ns)) - ymin) / (ymax - ymin) * plot_h

    blue, orange = "#1769aa", "#d95f02"
    s = ["<svg xmlns='http://www.w3.org/2000/svg' width='%d' height='%d' viewBox='0 0 %d %d'>"
         % (width, height, width, height),
         "<rect width='100%' height='100%' fill='white'/>",
         "<style>text{font-family:Arial,sans-serif;fill:#222}.grid{stroke:#e5e5e5}"
         ".axis{stroke:#333}.a{fill:none;stroke:%s;stroke-width:3}"
         ".b{fill:none;stroke:%s;stroke-width:3}</style>" % (blue, orange),
         "<text x='%d' y='28' text-anchor='middle' font-size='18' font-weight='bold'>"
         "Cancel latency vs resting-book size (%d price levels)</text>" % (width // 2, levels)]
    # y grid at decade lines
    lo, hi = int(math.floor(ymin)), int(math.ceil(ymax))
    for e in range(lo, hi + 1):
        yy = py(10 ** e)
        s.append("<line class='grid' x1='%d' y1='%.1f' x2='%d' y2='%.1f'/>" % (left, yy, left + plot_w, yy))
        label = f"{10**e:,}" if e < 6 else f"1e{e}"
        s.append("<text x='%d' y='%.1f' text-anchor='end' font-size='11'>%s</text>" % (left - 8, yy + 4, label))
    s.append("<line class='axis' x1='%d' y1='%d' x2='%d' y2='%d'/>" % (left, top, left, top + plot_h))
    s.append("<line class='axis' x1='%d' y1='%d' x2='%d' y2='%d'/>" % (left, top + plot_h, left + plot_w, top + plot_h))
    for r in rows:
        xx = px(r["size"])
        lab = f"{r['size']//1000}K" if r["size"] < 1_000_000 else f"{r['size']//1_000_000}M"
        s.append("<text x='%.1f' y='%d' text-anchor='middle' font-size='11'>%s</text>" % (xx, top + plot_h + 22, lab))
    pa = " ".join("%.1f,%.1f" % (px(r["size"]), py(r["babo_ns"])) for r in rows)
    pb = " ".join("%.1f,%.1f" % (px(r["size"]), py(r["liqui_ns"])) for r in rows)
    s.append("<polyline class='b' points='%s'/>" % pb)
    s.append("<polyline class='a' points='%s'/>" % pa)
    for r in rows:
        s.append("<circle cx='%.1f' cy='%.1f' r='4.5' fill='%s'/>" % (px(r["size"]), py(r["babo_ns"]), blue))
        s.append("<circle cx='%.1f' cy='%.1f' r='4.5' fill='%s'/>" % (px(r["size"]), py(r["liqui_ns"]), orange))
    s.append("<text transform='translate(22 %d) rotate(-90)' text-anchor='middle' font-size='13'>"
             "ns per cancel (log)</text>" % (top + plot_h // 2))
    s.append("<text x='%d' y='%d' text-anchor='middle' font-size='13'>Resting orders (log)</text>"
             % (left + plot_w // 2, height - 14))
    lx = left + plot_w + 20
    s.append("<line x1='%d' y1='70' x2='%d' y2='70' stroke='%s' stroke-width='3'/>"
             "<text x='%d' y='74' font-size='12'>babobook O(1)</text>" % (lx, lx + 26, blue, lx + 32))
    s.append("<line x1='%d' y1='92' x2='%d' y2='92' stroke='%s' stroke-width='3'/>"
             "<text x='%d' y='96' font-size='12'>liquibook O(depth)</text>" % (lx, lx + 26, orange, lx + 32))
    s.append("</svg>")
    path.write_text("\n".join(s), encoding="utf-8")


def markdown(meta: dict, rows: list[dict], label: str) -> str:
    c, g, m = meta["compiler"], meta["git"], meta["machine"]
    out = [
        "<!-- GENERATED by scripts/run_scaling.py; do not hand-edit. -->",
        "# babobook vs liquibook — cancel latency vs resting-book size", "",
        f"- **Label:** {label or 'none'}",
        f"- **Generated (UTC):** {meta['created_utc']}",
        f"- **CPU / OS:** {m['cpu']} — {m['os']}",
        f"- **Logical CPUs / RAM:** {m['logical_cpus']} / {m['memory_gib']} GiB",
        f"- **Compiler:** {c['cxx_compiler_id']} {c['cxx_compiler_version']}",
        f"- **CMake build type:** `{c['cmake_build_type']}`",
        f"- **Git:** `{g['commit']}` (branch `{g['branch']}`, dirty `{g['dirty']}`)",
        f"- **Setup:** {meta['levels']} price levels; N orders → depth ≈ N/{meta['levels']}; "
        f"cancel all N in a fixed shuffled order; best of {meta['reps']} reps; prefill off the clock.", "",
        "| Resting N | Depth/level | babo ns/cancel | liquibook ns/cancel | babo cancel speedup |",
        "|---:|---:|---:|---:|---:|",
    ]
    for r in rows:
        out.append(f"| {r['size']:,} | {r['depth']:,} | {r['babo_ns']:.1f} | "
                   f"{r['liqui_ns']:.1f} | {r['speedup']:.1f}× |")
    out += ["", f"![Cancel latency vs book size]({meta['svg_name']})", "",
            "> babo cancel is O(1) (id→slot hash index); its gentle rise is cache-hierarchy "
            "latency as the working set outgrows L2/L3 — a cost liquibook pays too, on top of "
            "its O(depth) `find_on_market` rescan. The speedup column is the money figure."]
    return "\n".join(out) + "\n"


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build-dir", required=True, help="CMake build directory (contains perf/)")
    ap.add_argument("--levels", type=int, default=64, help="price levels the orders pack into")
    ap.add_argument("--reps", type=int, default=3, help="best-of-N reps per size")
    ap.add_argument("--sizes", default="", help="comma-separated resting sizes (default: binary's 1k..200k)")
    ap.add_argument("--max", type=int, default=0, help="append one extra hero size, e.g. 1000000")
    ap.add_argument("--label", default="", help="machine label (default: this host's name)")
    ap.add_argument("--output-dir", default="", help="parent output directory")
    args = ap.parse_args()
    if args.reps < 1 or args.levels < 1:
        ap.error("--reps and --levels must be positive")
    if not args.label:                       # auto-tag with OS + CPU model
        args.label = default_label()

    script = Path(__file__).resolve()
    repo = script.parent.parent
    build_dir = Path(args.build_dir).resolve()
    perf_dir = build_dir / "perf" if (build_dir / "perf").is_dir() else build_dir
    babo_bin = utils.find_binary(build_dir, "babo_scaling")
    liqui_bin = utils.find_binary(build_dir, "liqui_scaling")

    parent = Path(args.output_dir).resolve() if args.output_dir else perf_dir / "results"
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    bundle = parent / f"scaling_{stamp}"
    bundle.mkdir(parents=True, exist_ok=False)

    babo = {r["size"]: r for r in run_engine(babo_bin, "babobook", args, bundle)}
    liqui = {r["size"]: r for r in run_engine(liqui_bin, "liquibook", args, bundle)}
    sizes = sorted(set(babo) & set(liqui))
    if not sizes:
        raise RuntimeError("no common sizes between the two engines")

    rows = []
    for n in sizes:
        b, l = babo[n], liqui[n]
        rows.append({
            "size": n, "levels": b["levels"], "depth": b["depth"],
            "babo_ns": b["ns_per_cancel"], "liqui_ns": l["ns_per_cancel"],
            "babo_mps": b["mps"], "liqui_mps": l["mps"],
            "babo_seconds": b["seconds"], "liqui_seconds": l["seconds"],
            "speedup": (l["ns_per_cancel"] / b["ns_per_cancel"]) if b["ns_per_cancel"] else 0.0,
        })

    mem = utils.memory_bytes()
    svg_name = "scaling.svg"
    meta = {
        "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "generated_by": "scripts/run_scaling.py",
        "runner_sha256": utils.sha256(script),
        "label": args.label, "levels": args.levels, "reps": args.reps,
        "sizes": sizes, "svg_name": svg_name,
        "machine": {"os": platform.platform(), "system": platform.system(),
                    "architecture": platform.machine(), "cpu": utils.cpu_name(),
                    "logical_cpus": os.cpu_count(), "memory_bytes": mem,
                    "memory_gib": round(mem / 2**30, 2) if mem else None,
                    "python": platform.python_version()},
        "compiler": utils.compiler_metadata(build_dir),
        "git": utils.git_metadata(repo),
        "binaries": {
            "babo_scaling": {"sha256": utils.sha256(babo_bin)},
            "liqui_scaling": {"sha256": utils.sha256(liqui_bin)},
        },
    }

    (bundle / "results.json").write_text(json.dumps({"metadata": meta, "results": rows}, indent=2) + "\n",
                                         encoding="utf-8")
    with (bundle / "results.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader(); w.writerows(rows)
    log_log_svg(rows, bundle / svg_name, args.levels)
    (bundle / "report.md").write_text(markdown(meta, rows, args.label), encoding="utf-8")

    artifacts = sorted(p for p in bundle.iterdir() if p.is_file() and p.name != "MANIFEST.sha256")
    (bundle / "MANIFEST.sha256").write_text(
        "".join(f"{utils.sha256(p)}  {p.name}\n" for p in artifacts), encoding="ascii")
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
