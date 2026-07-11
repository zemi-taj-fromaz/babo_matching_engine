#!/usr/bin/env bash
# Throughput matrix (market scenarios x message-scale) + paper-ready bundle.
# Thin wrapper over scripts/run_market_matrix.py; forwards flags, finds Python 3.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
runner="$script_dir/run_market_matrix.py"

build_dir=""; scenarios=""; counts=""; reps="10"; metric="peak"; label=""; output_dir=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)  build_dir="$2";  shift 2;;
        --scenarios)  scenarios="$2";  shift 2;;
        --counts)     counts="$2";     shift 2;;
        --reps)       reps="$2";       shift 2;;
        --metric)     metric="$2";     shift 2;;
        --label)      label="$2";      shift 2;;
        --output-dir) output_dir="$2"; shift 2;;
        -h|--help)
            echo "usage: $0 --build-dir <dir> [--scenarios a,b] [--counts 1000,...] [--reps R] [--metric peak|median|mean] [--label NAME]"
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

args=(--build-dir "$build_dir" --reps "$reps" --metric "$metric")
[[ -n "$scenarios" ]] && args+=(--scenarios "$scenarios")
[[ -n "$counts" ]] && args+=(--counts "$counts")
[[ -n "$label" ]] && args+=(--label "$label")
[[ -n "$output_dir" ]] && args+=(--output-dir "$output_dir")
exec "$python_bin" "$runner" "${args[@]}"
