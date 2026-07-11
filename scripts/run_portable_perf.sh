#!/usr/bin/env sh
set -eu

usage() {
  echo "usage: $0 --build-dir DIR [--reps 100] [--label TEXT] [--output-dir DIR] [--scenarios LIST] [--liquibook-first]" >&2
}

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if command -v python3 >/dev/null 2>&1; then PYTHON=python3
elif command -v python >/dev/null 2>&1; then PYTHON=python
else echo "Python 3 not found." >&2; exit 1
fi

[ "$#" -gt 0 ] || { usage; exit 2; }
exec "$PYTHON" "$SCRIPT_DIR/run_portable_perf.py" "$@"
