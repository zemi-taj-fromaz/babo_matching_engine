"""Shared, dependency-free helpers for the cross-platform result runners."""

from __future__ import annotations

import ctypes
import hashlib
import os
import platform
import re
import subprocess
import sys
from pathlib import Path


ANSI_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run_text(cmd: list[str], cwd: Path | None = None, check: bool = False) -> tuple[int, str]:
    try:
        process = subprocess.run(
            cmd, cwd=cwd, text=True, encoding="utf-8", errors="replace",
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
        )
        if check and process.returncode != 0:
            raise RuntimeError(f"command failed ({process.returncode}): {' '.join(cmd)}\n{process.stdout}")
        return process.returncode, process.stdout
    except OSError as exc:
        if check:
            raise RuntimeError(f"could not run {' '.join(cmd)}: {exc}") from exc
        return 127, str(exc)


def _cache_value(cache: str, key: str) -> str:
    match = re.search(rf"^{re.escape(key)}(?::[^=]+)?=(.*)$", cache, re.MULTILINE)
    return match.group(1).strip() if match else ""


def compiler_metadata(build_dir: Path) -> dict:
    cache_path = build_dir / "CMakeCache.txt"
    cache = cache_path.read_text(encoding="utf-8", errors="replace") if cache_path.exists() else ""
    compiler = _cache_value(cache, "CMAKE_CXX_COMPILER")
    version = ""
    if compiler:
        _, output = run_text([compiler, "--version"])
        if not output.strip() and os.name == "nt":
            _, output = run_text([compiler, "/Bv"])
        version = "\n".join(output.strip().splitlines()[:4])

    compiler_file = next(build_dir.glob("CMakeFiles/*/CMakeCXXCompiler.cmake"), None)
    compiler_id = ""
    compiler_version = ""
    if compiler_file:
        text = compiler_file.read_text(encoding="utf-8", errors="replace")
        match = re.search(r'set\(CMAKE_CXX_COMPILER_ID "([^"]+)"\)', text)
        compiler_id = match.group(1) if match else ""
        match = re.search(r'set\(CMAKE_CXX_COMPILER_VERSION "([^"]+)"\)', text)
        compiler_version = match.group(1) if match else ""

    return {
        "cmake_generator": _cache_value(cache, "CMAKE_GENERATOR"),
        "cmake_build_type": _cache_value(cache, "CMAKE_BUILD_TYPE"),
        "cxx_compiler": compiler,
        "cxx_compiler_id": compiler_id,
        "cxx_compiler_version": compiler_version,
        "cxx_compiler_version_output": version,
        "cxx_flags": _cache_value(cache, "CMAKE_CXX_FLAGS"),
        "cxx_flags_release": _cache_value(cache, "CMAKE_CXX_FLAGS_RELEASE"),
        "cmake_cache_sha256": sha256(cache_path) if cache_path.exists() else "",
        "pin_sweep_enabled": _cache_value(cache, "BABO_BUILD_PIN_SWEEP") or "OFF",
    }


def cpu_name() -> str:
    if os.name == "nt":
        try:
            import winreg
            with winreg.OpenKey(
                winreg.HKEY_LOCAL_MACHINE,
                r"HARDWARE\DESCRIPTION\System\CentralProcessor\0",
            ) as key:
                return str(winreg.QueryValueEx(key, "ProcessorNameString")[0]).strip()
        except OSError:
            pass
    if sys.platform == "darwin":
        rc, output = run_text(["sysctl", "-n", "machdep.cpu.brand_string"])
        if rc == 0 and output.strip():
            return output.strip()
    if Path("/proc/cpuinfo").exists():
        text = Path("/proc/cpuinfo").read_text(errors="replace")
        match = re.search(r"^(?:model name|Hardware)\s*:\s*(.+)$", text, re.MULTILINE)
        if match:
            return match.group(1).strip()
    return platform.processor() or "unknown"


def memory_bytes() -> int | None:
    if os.name == "nt":
        class MemStatus(ctypes.Structure):
            _fields_ = [
                ("length", ctypes.c_ulong), ("load", ctypes.c_ulong),
                ("total_phys", ctypes.c_ulonglong), ("avail_phys", ctypes.c_ulonglong),
                ("total_page", ctypes.c_ulonglong), ("avail_page", ctypes.c_ulonglong),
                ("total_virtual", ctypes.c_ulonglong), ("avail_virtual", ctypes.c_ulonglong),
                ("avail_extended", ctypes.c_ulonglong),
            ]
        status = MemStatus()
        status.length = ctypes.sizeof(status)
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
            return int(status.total_phys)
    if sys.platform == "darwin":
        rc, output = run_text(["sysctl", "-n", "hw.memsize"])
        if rc == 0 and output.strip().isdigit():
            return int(output.strip())
    try:
        return int(os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES"))
    except (AttributeError, ValueError, OSError):
        return None


def git_metadata(repo: Path) -> dict:
    commit_rc, commit = run_text(["git", "-C", str(repo), "rev-parse", "HEAD"])
    status_rc, status = run_text(["git", "-C", str(repo), "status", "--porcelain"])
    branch_rc, branch = run_text(["git", "-C", str(repo), "branch", "--show-current"])
    return {
        "commit": commit.strip() if commit_rc == 0 else "unavailable",
        "branch": branch.strip() if branch_rc == 0 else "unavailable",
        "dirty": bool(status.strip()) if status_rc == 0 else None,
        "status_available": status_rc == 0,
    }


def find_binary(build_dir: Path, name: str) -> Path:
    directories = [
        build_dir / "perf", build_dir / "src", build_dir,
        build_dir / "perf" / "Release", build_dir / "perf" / "RelWithDebInfo",
        build_dir / "src" / "Release", build_dir / "src" / "RelWithDebInfo",
    ]
    candidates = [directory / suffix for directory in directories for suffix in (name, f"{name}.exe")]
    for path in candidates:
        if path.is_file():
            return path.resolve()
    raise FileNotFoundError(f"could not find {name} under {build_dir}")
