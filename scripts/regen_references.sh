#!/usr/bin/env bash
# regen_references.sh - regenerate the correctness references from liquibook,
# persist them to the source tree, and verify babo matches on all five scenarios.
# Portable: Linux / macOS / Windows-git-bash (handles harness{,.exe}, .so/.dll/.dylib).
#
# Usage:
#   scripts/regen_references.sh [--bench-dir DIR] [--count N]
#   (run from anywhere; auto-detects the newest cmake-build-*/benchmark if --bench-dir omitted)
set -uo pipefail

COUNT=1000000
BENCHDIR=""
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC_REF="$REPO_ROOT/benchmark/reference/correctness_hash.txt"
SCENARIOS=(static normal swing-25 swing-40 flash-crash)

while [ $# -gt 0 ]; do
  case "$1" in
    --bench-dir) BENCHDIR="$2"; shift 2 ;;
    --count)     COUNT="$2";    shift 2 ;;
    -h|--help)   echo "usage: $0 [--bench-dir DIR] [--count N]"; exit 0 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

# --- locate the benchmark dir (no hardcoded build-dir name) ---
if [ -z "$BENCHDIR" ]; then
  if [ -f "./harness" ] || [ -f "./harness.exe" ]; then
    BENCHDIR="$PWD"
  else
    h=$(ls -t "$REPO_ROOT"/cmake-build-*/benchmark/harness "$REPO_ROOT"/cmake-build-*/benchmark/harness.exe 2>/dev/null | head -1)
    [ -n "$h" ] && BENCHDIR="$(dirname "$h")"
  fi
fi
[ -n "$BENCHDIR" ] || { echo "harness not found; pass --bench-dir <dir>" >&2; exit 1; }
BENCHDIR="$(cd "$BENCHDIR" && pwd)"

if   [ -f "$BENCHDIR/harness.exe" ]; then HARNESS="./harness.exe"
elif [ -f "$BENCHDIR/harness"     ]; then HARNESS="./harness"
else echo "harness not found in $BENCHDIR - build it first" >&2; exit 1; fi
echo "Using harness in: $BENCHDIR"

cd "$BENCHDIR"

# --baseline liquibook loads ./liquibook_adapter.<ext> from the cwd; the harness
# POST_BUILD stages it, but copy defensively across the possible extensions.
for ext in so dll dylib; do
  [ -f "adapters/liquibook_adapter.$ext" ] && cp -f "adapters/liquibook_adapter.$ext" .
done

echo "== Writing references (count=$COUNT) from liquibook =="
for s in "${SCENARIOS[@]}"; do
  "$HARNESS" --baseline liquibook --scenario "$s" --count "$COUNT" --write-reference >/dev/null 2>&1
  echo "  wrote $s"
done

# Persist to the source tree so a rebuild's POST_BUILD copy does not revert it.
cp -f reference/correctness_hash.txt "$SRC_REF"
echo "Persisted references -> $SRC_REF"

echo "== Verifying babo matches =="
BABO=""
for ext in so dll dylib; do
  if [ -f "adapters/babobook_adapter.$ext" ]; then
    cp -f "adapters/babobook_adapter.$ext" .
    BABO="./babobook_adapter.$ext"
  fi
done
if [ -z "$BABO" ]; then echo "  (babobook adapter not built - skipping babo verification)"; exit 0; fi

for s in "${SCENARIOS[@]}"; do
  out=$("$HARNESS" --engine "$BABO" --scenario "$s" --count "$COUNT" --mode perf 2>/dev/null)
  status=$(printf '%s\n' "$out"  | grep -oE 'Status: *[A-Za-z_]+'  | awk '{print $NF}')
  verdict=$(printf '%s\n' "$out" | grep -oE 'Verdict: *[A-Za-z]+' | awk '{print $NF}')
  printf "  %-12s %-8s %s\n" "$s" "${status:-?}" "${verdict:-?}"
done
