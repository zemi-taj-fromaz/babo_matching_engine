#!/usr/bin/env python3
"""Run the canonical babobook/Liquibook perf matrix and create a shareable bundle."""

from __future__ import annotations

import argparse
import csv
import ctypes
import datetime as dt
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

FORMAT_VERSION = 1
SCENARIOS = ["static", "normal", "swing25", "flash_crash_40", "flash_crash_60"]
DISPLAY = {
    "static": "Static",
    "normal": "Normal",
    "swing25": "Swing-25",
    "flash_crash_40": "Swing-40",
    "flash_crash_60": "Flash-crash-60",
}
ANSI_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def run_text(cmd: list[str], cwd: Path | None = None, check: bool = False) -> tuple[int, str]:
    try:
        p = subprocess.run(cmd, cwd=cwd, text=True, encoding="utf-8", errors="replace",
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
        if check and p.returncode != 0:
            raise RuntimeError(f"command failed ({p.returncode}): {' '.join(cmd)}\n{p.stdout}")
        return p.returncode, p.stdout
    except OSError as exc:
        if check:
            raise RuntimeError(f"could not run {' '.join(cmd)}: {exc}") from exc
        return 127, str(exc)


def cache_value(cache: str, key: str) -> str:
    m = re.search(rf"^{re.escape(key)}(?::[^=]+)?=(.*)$", cache, re.MULTILINE)
    return m.group(1).strip() if m else ""


def compiler_metadata(build_dir: Path) -> dict:
    cache_path = build_dir / "CMakeCache.txt"
    cache = cache_path.read_text(encoding="utf-8", errors="replace") if cache_path.exists() else ""
    compiler = cache_value(cache, "CMAKE_CXX_COMPILER")
    version = ""
    if compiler:
        _, out = run_text([compiler, "--version"])
        if not out.strip() and os.name == "nt":
            _, out = run_text([compiler, "/Bv"])
        version = "\n".join(out.strip().splitlines()[:4])
    compiler_file = next(build_dir.glob("CMakeFiles/*/CMakeCXXCompiler.cmake"), None)
    compiler_id = ""
    compiler_version = ""
    if compiler_file:
        text = compiler_file.read_text(encoding="utf-8", errors="replace")
        m = re.search(r'set\(CMAKE_CXX_COMPILER_ID "([^"]+)"\)', text)
        compiler_id = m.group(1) if m else ""
        m = re.search(r'set\(CMAKE_CXX_COMPILER_VERSION "([^"]+)"\)', text)
        compiler_version = m.group(1) if m else ""
    return {
        "cmake_generator": cache_value(cache, "CMAKE_GENERATOR"),
        "cmake_build_type": cache_value(cache, "CMAKE_BUILD_TYPE"),
        "cxx_compiler": compiler,
        "cxx_compiler_id": compiler_id,
        "cxx_compiler_version": compiler_version,
        "cxx_compiler_version_output": version,
        "cxx_flags": cache_value(cache, "CMAKE_CXX_FLAGS"),
        "cxx_flags_release": cache_value(cache, "CMAKE_CXX_FLAGS_RELEASE"),
        "cmake_cache_sha256": sha256(cache_path) if cache_path.exists() else "",
        "pin_sweep_enabled": cache_value(cache, "BABO_BUILD_PIN_SWEEP") or "OFF",
    }


def cpu_name() -> str:
    if os.name == "nt":
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                                r"HARDWARE\DESCRIPTION\System\CentralProcessor\0") as key:
                return str(winreg.QueryValueEx(key, "ProcessorNameString")[0]).strip()
        except OSError:
            pass
    if sys.platform == "darwin":
        rc, out = run_text(["sysctl", "-n", "machdep.cpu.brand_string"])
        if rc == 0 and out.strip():
            return out.strip()
    if Path("/proc/cpuinfo").exists():
        text = Path("/proc/cpuinfo").read_text(errors="replace")
        m = re.search(r"^(?:model name|Hardware)\s*:\s*(.+)$", text, re.MULTILINE)
        if m:
            return m.group(1).strip()
    return platform.processor() or "unknown"


def memory_bytes() -> int | None:
    if os.name == "nt":
        class MemStatus(ctypes.Structure):
            _fields_ = [("length", ctypes.c_ulong), ("load", ctypes.c_ulong),
                        ("total_phys", ctypes.c_ulonglong), ("avail_phys", ctypes.c_ulonglong),
                        ("total_page", ctypes.c_ulonglong), ("avail_page", ctypes.c_ulonglong),
                        ("total_virtual", ctypes.c_ulonglong), ("avail_virtual", ctypes.c_ulonglong),
                        ("avail_extended", ctypes.c_ulonglong)]
        status = MemStatus()
        status.length = ctypes.sizeof(status)
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
            return int(status.total_phys)
    if sys.platform == "darwin":
        rc, out = run_text(["sysctl", "-n", "hw.memsize"])
        if rc == 0 and out.strip().isdigit():
            return int(out.strip())
    try:
        return int(os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES"))
    except (AttributeError, ValueError, OSError):
        return None


def git_metadata(repo: Path) -> dict:
    rc, commit = run_text(["git", "-C", str(repo), "rev-parse", "HEAD"])
    rc2, status = run_text(["git", "-C", str(repo), "status", "--porcelain"])
    rc3, branch = run_text(["git", "-C", str(repo), "branch", "--show-current"])
    return {
        "commit": commit.strip() if rc == 0 else "unavailable",
        "branch": branch.strip() if rc3 == 0 else "unavailable",
        "dirty": bool(status.strip()) if rc2 == 0 else None,
        "status_available": rc2 == 0,
    }


def find_binary(build_dir: Path, name: str) -> Path:
    dirs = [build_dir / "perf", build_dir / "src", build_dir,
            build_dir / "perf" / "Release", build_dir / "perf" / "RelWithDebInfo",
            build_dir / "src" / "Release", build_dir / "src" / "RelWithDebInfo"]
    candidates = [directory / suffix for directory in dirs for suffix in (name, f"{name}.exe")]
    for path in candidates:
        if path.is_file():
            return path.resolve()
    raise FileNotFoundError(f"could not find {name} under {build_dir}")


def parse_perf_output(text: str, expected_reps: int, expected_keys: list[str]) -> list[dict]:
    clean = ANSI_RE.sub("", text).replace("\r", "")
    affinity_ok = "could not pin" not in clean.lower()
    priority_ok = "could not raise priority" not in clean.lower()
    core_match = re.search(r"pinned to (?:CPU )?core\s+(\d+)", clean, re.IGNORECASE)
    capacity_match = re.search(r"pin_node capacity:\s*(\d+)", clean)
    block_re = re.compile(
        r"---\s+([A-Za-z0-9_-]+)\s+-+.*?"
        r"messages\s+([0-9,]+)\s+x\s+(\d+)\s+reps\s+=\s+([0-9,]+).*?"
        r"wall time\s+([0-9.]+)\s+s.*?"
        r"THROUGHPUT\s+([0-9.]+)\s+M msgs/s.*?"
        r"resting book\s+([0-9,]+)", re.DOTALL)
    rows = []
    for m in block_re.finditer(clean):
        reps = int(m.group(3))
        if reps != expected_reps:
            raise RuntimeError(f"output reports {reps} reps; expected {expected_reps}")
        rows.append({
            "scenario_key": m.group(1),
            "scenario": DISPLAY.get(m.group(1), m.group(1)),
            "messages_per_rep": int(m.group(2).replace(",", "")),
            "reps": reps,
            "total_messages": int(m.group(4).replace(",", "")),
            "wall_seconds": float(m.group(5)),
            "throughput_mmsg_s": float(m.group(6)),
            "resting_orders": int(m.group(7).replace(",", "")),
            "affinity_ok": affinity_ok, "priority_ok": priority_ok,
            "pinned_core": int(core_match.group(1)) if core_match else None,
            "pin_capacity": int(capacity_match.group(1)) if capacity_match else None,
        })
    # Compatibility with earlier canonical perf output retained in older build
    # directories. Fresh builds use the formatted block above.
    if not rows:
        legacy_re = re.compile(
            r"(?:babo|liqui(?:book)?) perf:\s*([A-Za-z0-9_-]+):\s*([0-9,]+) messages x (\d+) reps.*?"
            r"messages\s*:\s*([0-9,]+).*?wall time\s*:\s*([0-9.]+) s.*?"
            r"throughput\s*:\s*([0-9.]+) M msgs/s.*?resting\s*:\s*([0-9,]+)", re.DOTALL | re.IGNORECASE)
        for m in legacy_re.finditer(clean):
            reps = int(m.group(3))
            if reps != expected_reps:
                raise RuntimeError(f"output reports {reps} reps; expected {expected_reps}")
            messages = int(m.group(2).replace(",", ""))
            rows.append({
                "scenario_key": m.group(1),
                "scenario": DISPLAY.get(m.group(1), m.group(1)),
                "messages_per_rep": messages,
                "reps": reps,
                "total_messages": messages * reps,
                "wall_seconds": float(m.group(5)),
                "throughput_mmsg_s": float(m.group(6)),
                "resting_orders": int(m.group(7).replace(",", "")),
                "affinity_ok": affinity_ok, "priority_ok": priority_ok,
                "pinned_core": int(core_match.group(1)) if core_match else None,
                "pin_capacity": int(capacity_match.group(1)) if capacity_match else None,
            })
    if [r["scenario_key"] for r in rows] != expected_keys:
        raise RuntimeError(f"could not parse all scenarios; found {[r['scenario_key'] for r in rows]}")
    return rows


def run_engine(binary: Path, engine_label: str, scenarios: list[str], reps: int, bundle: Path) -> list[dict]:
    rows: list[dict] = []
    combined: list[str] = []
    safe_label = engine_label.lower()
    for index, scenario in enumerate(scenarios, 1):
        print(f"  [{index}/{len(scenarios)}] {DISPLAY[scenario]}...", flush=True)
        cmd = [str(binary), "--scenario", scenario, "--reps", str(reps)]
        rc, output = run_text(cmd, cwd=binary.parent)
        raw_path = bundle / f"raw_{safe_label}_{scenario}.txt"
        raw_path.write_text(output, encoding="utf-8")
        combined.append(f"===== {scenario} =====\n{output}")
        (bundle / f"raw_{safe_label}.txt").write_text("\n".join(combined), encoding="utf-8")
        if rc != 0:
            raise RuntimeError(f"{binary.name} failed ({rc}); see {raw_path}")
        rows.extend(parse_perf_output(output, reps, [scenario]))
    return rows


def markdown_report(meta: dict, rows: list[dict], label: str) -> str:
    c = meta["compiler"]
    g = meta["git"]
    lines = [
        "<!-- GENERATED by scripts/run_portable_perf.py; do not hand-edit. -->",
        "# babobook vs Liquibook — portable perf matrix", "",
        f"- **Run label:** {label or 'none'}",
        f"- **Generated (UTC):** {meta['created_utc']}",
        f"- **OS:** {meta['machine']['os']}",
        f"- **CPU:** {meta['machine']['cpu']}",
        f"- **Logical CPUs / RAM:** {meta['machine']['logical_cpus']} / {meta['machine']['memory_gib']} GiB",
        f"- **Compiler:** {c['cxx_compiler_id']} {c['cxx_compiler_version']} (`{c['cxx_compiler']}`)",
        f"- **CMake:** {c['cmake_generator']}; build type `{c['cmake_build_type']}`",
        f"- **Git:** `{g['commit']}`; branch `{g['branch']}`; dirty `{g['dirty']}`",
        f"- **Protocol:** one warmup, then {meta['reps']} measured replays per scenario; core-pinned by each binary",
        f"- **Execution order:** {' → '.join(meta['execution_order'])}", "",
        f"- **Affinity / priority:** babobook `{meta['runtime']['babobook_affinity_ok']}` / `{meta['runtime']['babobook_priority_ok']}`; Liquibook `{meta['runtime']['liquibook_affinity_ok']}` / `{meta['runtime']['liquibook_priority_ok']}`", "",
        "| Scenario | Messages/rep | babobook M/s | Liquibook M/s | Speedup | babobook rest | Liquibook rest |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    for row in rows:
        lines.append(f"| {row['scenario']} | {row['messages_per_rep']:,} | "
                     f"{row['babobook_mmsg_s']:.2f} | {row['liquibook_mmsg_s']:.2f} | "
                     f"{row['speedup']:.2f}× | {row['babobook_resting']:,} | {row['liquibook_resting']:,} |")
    lines += ["", "## Artifact identity", "",
              f"- `babo_perf` SHA-256: `{meta['binaries']['babo_perf']['sha256']}`",
              f"- `liqui_perf` SHA-256: `{meta['binaries']['liqui_perf']['sha256']}`",
              f"- `CMakeCache.txt` SHA-256: `{c['cmake_cache_sha256']}`",
              f"- Runner SHA-256: `{meta['runner_sha256']}`", "",
              "Raw console output, machine-readable JSON/CSV, and `MANIFEST.sha256` are included in the same bundle.",
              "The hashes make accidental mixing of binaries/results visible; they are not a cryptographic attestation against deliberate editing."]
    return "\n".join(lines) + "\n"


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if hasattr(sys.stderr, "reconfigure"):
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build-dir", required=True, help="CMake build directory (contains perf/)")
    ap.add_argument("--reps", type=int, default=100)
    ap.add_argument("--label", default="", help="Optional contributor/machine label")
    ap.add_argument("--output-dir", default="", help="Parent output directory")
    ap.add_argument("--liquibook-first", action="store_true", help="Reverse recorded execution order")
    ap.add_argument("--scenarios", default=",".join(SCENARIOS),
                    help="Comma-separated subset for diagnostics; default is all five")
    args = ap.parse_args()
    if args.reps < 1:
        ap.error("--reps must be positive")
    scenarios = [x.strip() for x in args.scenarios.split(",") if x.strip()]
    if not scenarios or any(x not in SCENARIOS for x in scenarios) or len(set(scenarios)) != len(scenarios):
        ap.error(f"--scenarios must be a unique comma-separated subset of {','.join(SCENARIOS)}")

    script = Path(__file__).resolve()
    repo = script.parent.parent
    build_dir = Path(args.build_dir).resolve()
    perf_dir = build_dir / "perf" if (build_dir / "perf").is_dir() else build_dir
    babo_bin = find_binary(build_dir, "babo_perf")
    liqui_bin = find_binary(build_dir, "liqui_perf")
    parent = Path(args.output_dir).resolve() if args.output_dir else perf_dir / "results"
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    bundle = parent / f"portable_perf_{stamp}"
    bundle.mkdir(parents=True, exist_ok=False)

    order = [("babobook", babo_bin), ("Liquibook", liqui_bin)]
    if args.liquibook_first:
        order.reverse()
    measured: dict[str, list[dict]] = {}
    for display, binary in order:
        print(f"Running {display}: {len(scenarios)} scenario(s) × {args.reps} reps", flush=True)
        measured[display] = run_engine(binary, display, scenarios, args.reps, bundle)

    babo_by = {r["scenario_key"]: r for r in measured["babobook"]}
    liqui_by = {r["scenario_key"]: r for r in measured["Liquibook"]}
    rows = []
    for key in scenarios:
        b, l = babo_by[key], liqui_by[key]
        if b["messages_per_rep"] != l["messages_per_rep"]:
            raise RuntimeError(f"message-count mismatch in {key}")
        rows.append({
            "scenario_key": key, "scenario": DISPLAY[key],
            "messages_per_rep": b["messages_per_rep"], "reps": args.reps,
            "babobook_mmsg_s": b["throughput_mmsg_s"],
            "liquibook_mmsg_s": l["throughput_mmsg_s"],
            "speedup": b["throughput_mmsg_s"] / l["throughput_mmsg_s"],
            "babobook_wall_seconds": b["wall_seconds"],
            "liquibook_wall_seconds": l["wall_seconds"],
            "babobook_resting": b["resting_orders"],
            "liquibook_resting": l["resting_orders"],
            "babobook_affinity_ok": b["affinity_ok"],
            "liquibook_affinity_ok": l["affinity_ok"],
            "babobook_priority_ok": b["priority_ok"],
            "liquibook_priority_ok": l["priority_ok"],
            "pinned_core": b["pinned_core"],
            "babobook_pin_capacity": b["pin_capacity"],
        })

    mem = memory_bytes()
    meta = {
        "format_version": FORMAT_VERSION,
        "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "generated_by": "scripts/run_portable_perf.py",
        "runner_sha256": sha256(script),
        "label": args.label,
        "reps": args.reps,
        "scenarios": scenarios,
        "execution_order": [x[0] for x in order],
        "machine": {"os": platform.platform(), "system": platform.system(),
                    "release": platform.release(), "architecture": platform.machine(),
                    "cpu": cpu_name(), "logical_cpus": os.cpu_count(),
                    "memory_bytes": mem, "memory_gib": round(mem / 2**30, 2) if mem else None,
                    "python": platform.python_version()},
        "compiler": compiler_metadata(build_dir),
        "git": git_metadata(repo),
        "binaries": {
            "babo_perf": {"path": str(babo_bin), "size": babo_bin.stat().st_size, "sha256": sha256(babo_bin)},
            "liqui_perf": {"path": str(liqui_bin), "size": liqui_bin.stat().st_size, "sha256": sha256(liqui_bin)},
        },
        "runtime": {
            "babobook_affinity_ok": all(r["affinity_ok"] for r in measured["babobook"]),
            "liquibook_affinity_ok": all(r["affinity_ok"] for r in measured["Liquibook"]),
            "babobook_priority_ok": all(r["priority_ok"] for r in measured["babobook"]),
            "liquibook_priority_ok": all(r["priority_ok"] for r in measured["Liquibook"]),
        },
    }
    document = {"metadata": meta, "results": rows}
    (bundle / "results.json").write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    with (bundle / "results.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader(); writer.writerows(rows)
    (bundle / "report.md").write_text(markdown_report(meta, rows, args.label), encoding="utf-8")

    artifacts = sorted(p for p in bundle.iterdir() if p.is_file() and p.name != "MANIFEST.sha256")
    manifest = "".join(f"{sha256(p)}  {p.name}\n" for p in artifacts)
    (bundle / "MANIFEST.sha256").write_text(manifest, encoding="ascii")
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
