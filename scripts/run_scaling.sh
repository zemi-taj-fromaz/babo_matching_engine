#!/usr/bin/env bash
# Cancel-latency vs resting-book-size sweep + shareable bundle (Linux/macOS/git-bash).
# Thin wrapper over scripts/run_scaling.py; forwards flags and finds a Python 3.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
runner="$script_dir/run_scaling.py"

build_dir=""; levels="64"; reps="3"; sizes=""; max="0"; label=""; output_dir=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)  build_dir="$2"; shift 2;;
        --levels)     levels="$2";    shift 2;;
        --reps)       reps="$2";      shift 2;;
        --sizes)      sizes="$2";     shift 2;;
        --max)        max="$2";       shift 2;;
        --label)      label="$2";     shift 2;;
        --output-dir) output_dir="$2"; shift 2;;
        -h|--help)
            echo "usage: $0 --build-dir <dir> [--levels L] [--reps R] [--sizes a,b,c] [--max M] [--label NAME]"
            exit 0;;
        *) echo "unknown argument: $1" >&2; exit 2;;
    esac
done
if [[ -z "$build_dir" ]]; then echo "error: --build-dir is required" >&2; exit 2; fi

python_bin=""
for c in python3 python; do
    if command -v "$c" >/dev/null 2>&1 && "$c" -c 'import sys; assert sys.version_info.major==3' 2>/dev/null; then
        python_bin="$c"; break
    fi
done
if [[ -z "$python_bin" ]]; then echo "error: Python 3 not found (tried python3, python)" >&2; exit 1; fi

args=(--build-dir "$build_dir" --levels "$levels" --reps "$reps")
[[ -n "$sizes" ]] && args+=(--sizes "$sizes")
[[ "$max" -gt 0 ]] && args+=(--max "$max")
[[ -n "$label" ]] && args+=(--label "$label")
[[ -n "$output_dir" ]] && args+=(--output-dir "$output_dir")
exec "$python_bin" "$runner" "${args[@]}"
