#!/usr/bin/env bash
# compare_engines.sh - run all five scenarios for an engine adapter AND the
# liquibook baseline, capture throughput + correctness, and emit a console table
# plus CSV and Markdown reports (ready to paste into a README).
# Portable: Linux / macOS / Windows-git-bash (handles harness{,.exe}, .so/.dll/.dylib).
#
# Usage:
#   scripts/compare_engines.sh --engine adapters/babobook_adapter.so \
#       [--baseline adapters/liquibook_adapter.so] [--bench-dir DIR] \
#       [--count N] [--reps N]
#
# --engine / --baseline are paths RELATIVE TO the benchmark build dir (where the
# adapters/ folder and harness live). Baseline defaults to the built liquibook.
set -uo pipefail

ENGINE=""
BASELINE=""
BENCHDIR=""
COUNT=1000000
REPS=3
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SCENARIOS=(static normal swing-25 swing-40 flash-crash)

while [ $# -gt 0 ]; do
  case "$1" in
    --engine)    ENGINE="$2";   shift 2 ;;
    --baseline)  BASELINE="$2"; shift 2 ;;
    --bench-dir) BENCHDIR="$2"; shift 2 ;;
    --count)     COUNT="$2";    shift 2 ;;
    --reps)      REPS="$2";     shift 2 ;;
    -h|--help)   echo "usage: $0 --engine <adapter> [--baseline A] [--bench-dir DIR] [--count N] [--reps N]"; exit 0 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done
[ -n "$ENGINE" ] || { echo "error: --engine <adapter path> is required" >&2; exit 2; }

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

# default baseline: the built liquibook adapter (whichever extension exists)
if [ -z "$BASELINE" ]; then
  for ext in so dll dylib; do
    [ -f "$BENCHDIR/adapters/liquibook_adapter.$ext" ] && { BASELINE="adapters/liquibook_adapter.$ext"; break; }
  done
fi
[ -n "$BASELINE" ] || { echo "no baseline adapter found; pass --baseline <path>" >&2; exit 1; }

cd "$BENCHDIR"

# stage both adapters next to the harness so --engine ./name resolves
for p in "$ENGINE" "$BASELINE"; do
  [ -f "$p" ] && cp -f "$p" .
done
ENG="./$(basename "$ENGINE")"
BASE="./$(basename "$BASELINE")"
engName="$(basename "$ENGINE")";   engName="${engName%.*}"
baseName="$(basename "$BASELINE")"; baseName="${baseName%.*}"

# run_perf <dll> <scenario>  -> echoes "throughput trades status verdict" (best of REPS)
run_perf() {
  local dll="$1" scen="$2" best_tp=0 best="0 0 ? ?"
  local i out tp trades status verdict
  for ((i=0; i<REPS; i++)); do
    out=$("$HARNESS" --engine "$dll" --scenario "$scen" --count "$COUNT" --mode perf 2>/dev/null)
    tp=$(printf '%s\n' "$out" | grep -oE 'Throughput: *[0-9.]+' | grep -oE '[0-9.]+' | head -1)
    tp=${tp:-0}
    if awk "BEGIN{exit !($tp >= $best_tp)}"; then
      best_tp=$tp
      trades=$(printf '%s\n' "$out"  | grep -oE 'Trades emitted: *[0-9]+' | grep -oE '[0-9]+' | head -1)
      status=$(printf '%s\n' "$out"  | grep -oE 'Status: *[A-Za-z_]+'     | awk '{print $NF}')
      verdict=$(printf '%s\n' "$out" | grep -oE 'Verdict: *[A-Za-z]+'    | awk '{print $NF}')
      best="$tp ${trades:-0} ${status:-?} ${verdict:-?}"
    fi
  done
  echo "$best"
}

stamp=$(date +%Y%m%dT%H%M%S)
outdir="$BENCHDIR/results"; mkdir -p "$outdir"
csv="$outdir/compare_${engName}_${stamp}.csv"
md="$outdir/compare_${engName}_${stamp}.md"

echo "Comparing $engName vs $baseName  (count=$COUNT, best of $REPS)"

# CSV + Markdown headers
printf 'Scenario,%s_Mps,%s_Mps,Speedup,Trades,Correctness,Verdict\n' "$baseName" "$engName" > "$csv"
{
  echo "# Throughput: $engName vs $baseName"
  echo ""
  echo "- Messages/run: $COUNT (best of $REPS perf runs)"
  echo "- Build dir: \`$BENCHDIR\`"
  echo "- Generated: $stamp"
  echo ""
  echo "| Scenario | $baseName (M/s) | $engName (M/s) | Speedup | Trades | Correctness | Verdict |"
  echo "|---|---|---|---|---|---|---|"
} > "$md"

printf "%-12s %12s %12s %9s  %s\n" "Scenario" "$baseName" "$engName" "Speedup" "Correctness"
worst_scen=""; worst_tp=""
for s in "${SCENARIOS[@]}"; do
  read -r btp _ _ _              <<< "$(run_perf "$BASE" "$s")"
  read -r etp etrades est everd  <<< "$(run_perf "$ENG"  "$s")"
  btp=${btp:-0}; etp=${etp:-0}
  speedup=$(awk "BEGIN{ if($btp>0) printf \"%.2f\", $etp/$btp; else printf \"0\" }")
  printf "%-12s %12s %12s %8sx  %s\n" "$s" "$btp" "$etp" "$speedup" "${est:-?}"
  echo "$s,$btp,$etp,$speedup,${etrades:-0},${est:-?},${everd:-?}" >> "$csv"
  echo "| $s | $btp | $etp | ${speedup}x | ${etrades:-0} | ${est:-?} | ${everd:-?} |" >> "$md"
  if [ -z "$worst_tp" ] || awk "BEGIN{exit !($etp < $worst_tp)}"; then worst_tp=$etp; worst_scen=$s; fi
done

{
  echo ""
  echo "**Worst-case for $engName:** $worst_scen at $worst_tp M/s."
  echo ""
  echo "> A venue must survive its worst regime, so the worst-case row is the definitional number."
} >> "$md"

echo ""
echo "CSV report:      $csv"
echo "Markdown report: $md"
