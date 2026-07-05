# compare_engines.ps1 - run all five scenarios for an engine adapter AND the
# liquibook baseline, capture throughput + correctness, and emit a console table
# plus CSV and Markdown reports (ready to paste into a README).
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\compare_engines.ps1 `
#       -Engine adapters\babobook_adapter.dll `
#       -BenchDir cmake-build-release-system\benchmark
#
#   # quick smoke:
#   ... -Engine adapters\babobook_adapter.dll -Count 100000 -Reps 1
#
# -Engine / -Baseline are paths RELATIVE TO the benchmark build dir (where the
# adapters\ folder and harness.exe live). Baseline defaults to liquibook.
param(
    [Parameter(Mandatory=$true)][string]$Engine,
    [string]$Baseline = "adapters\liquibook_adapter.dll",
    [string]$BenchDir = "",
    [int]$Count = 1000000,
    [int]$Reps  = 3
)
$ErrorActionPreference = "Stop"
$scenarios = "static","normal","swing-25","swing-40","flash-crash"
$repoRoot  = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

# --- locate the harness (same rule as regen_references.ps1) ---
if (-not $BenchDir) {
    if (Test-Path (Join-Path $PWD.Path "harness.exe")) { $BenchDir = $PWD.Path }
    else {
        $hit = Get-ChildItem $repoRoot -Directory -Filter "cmake-build-*" -ErrorAction SilentlyContinue |
               ForEach-Object { Join-Path $_.FullName "benchmark\harness.exe" } |
               Where-Object { Test-Path $_ } | Get-Item |
               Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($hit) { $BenchDir = $hit.DirectoryName } else { throw "harness.exe not found - pass -BenchDir <dir>." }
    }
}
$BenchDir = (Resolve-Path $BenchDir).Path
if (-not (Test-Path (Join-Path $BenchDir "harness.exe"))) { throw "harness.exe not found in $BenchDir - build it first." }

$engName  = [IO.Path]::GetFileNameWithoutExtension($Engine)
$baseName = [IO.Path]::GetFileNameWithoutExtension($Baseline)

# Run perf $Reps times for one dll+scenario, keep the best (max throughput).
function Invoke-Perf($dll, $scenario) {
    # The harness's progress/pin lines go to stderr; Windows PowerShell would
    # treat those as terminating errors under the script's -Stop. Make this
    # function tolerant (function-scoped) and parse the merged stdout+stderr text.
    $ErrorActionPreference = 'Continue'
    $best = [pscustomobject]@{ Throughput=0.0; Trades=0; Status="?"; Verdict="?" }
    for ($i = 0; $i -lt $Reps; $i++) {
        $text = (& .\harness.exe --engine $dll --scenario $scenario --count $Count --mode perf 2>&1 | Out-String)
        $tp = if ($text -match 'Throughput:\s*([\d.]+)') { [double]$Matches[1] } else { 0.0 }
        if ($tp -ge $best.Throughput) {
            $best.Throughput = $tp
            if ($text -match 'Trades emitted:\s*(\d+)') { $best.Trades  = [int64]$Matches[1] }
            if ($text -match 'Status:\s*(\S+)')         { $best.Status  = $Matches[1] }
            if ($text -match 'Verdict:\s*(\S+)')        { $best.Verdict = $Matches[1] }
        }
    }
    return $best
}

Push-Location $BenchDir
try {
    # Stage both dlls next to harness.exe so --engine .\name.dll resolves.
    foreach ($p in @($Engine, $Baseline)) {
        if (Test-Path $p) { Copy-Item $p . -Force -ErrorAction SilentlyContinue }
        elseif (-not (Test-Path (Split-Path $p -Leaf))) { throw "dll not found: $p (relative to $BenchDir)" }
    }
    $engDll  = ".\" + (Split-Path $Engine   -Leaf)
    $baseDll = ".\" + (Split-Path $Baseline -Leaf)

    Write-Host "Comparing $engName vs $baseName  (count=$Count, best of $Reps)" -ForegroundColor Cyan
    $rows = @()
    foreach ($s in $scenarios) {
        Write-Host ("  {0,-12} ..." -f $s) -NoNewline
        $b = Invoke-Perf $baseDll $s
        $e = Invoke-Perf $engDll  $s
        $speedup = if ($b.Throughput -gt 0) { [math]::Round($e.Throughput / $b.Throughput, 2) } else { 0 }
        $rows += [pscustomobject]@{
            Scenario    = $s
            Baseline_Mps = $b.Throughput
            Engine_Mps   = $e.Throughput
            Speedup      = $speedup
            Trades       = $e.Trades
            Correctness  = $e.Status
            Verdict      = $e.Verdict
        }
        Write-Host (" base={0} eng={1}  {2}x  [{3}]" -f $b.Throughput, $e.Throughput, $speedup, $e.Status)
    }

    Write-Host ""
    $rows | Format-Table -AutoSize | Out-String | Write-Host

    # --- write CSV + Markdown ---
    $stamp  = Get-Date -Format "yyyyMMddTHHmmss"
    $outDir = Join-Path $BenchDir "results"
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    $csv = Join-Path $outDir ("compare_{0}_{1}.csv" -f $engName, $stamp)
    $md  = Join-Path $outDir ("compare_{0}_{1}.md"  -f $engName, $stamp)
    $rows | Export-Csv -NoTypeInformation -Path $csv

    $worst = $rows | Sort-Object Engine_Mps | Select-Object -First 1
    $md_lines = @(
        "# Throughput: $engName vs $baseName",
        "",
        "- Messages/run: $Count (best of $Reps perf runs)",
        "- Build dir: ``$BenchDir``",
        "- Generated: $stamp",
        "",
        "| Scenario | $baseName (M/s) | $engName (M/s) | Speedup | Trades | Correctness | Verdict |",
        "|---|---|---|---|---|---|---|"
    )
    foreach ($r in $rows) {
        $md_lines += "| $($r.Scenario) | $($r.Baseline_Mps) | $($r.Engine_Mps) | $($r.Speedup)x | $($r.Trades) | $($r.Correctness) | $($r.Verdict) |"
    }
    $md_lines += @(
        "",
        "**Worst-case for ${engName}:** $($worst.Scenario) at $($worst.Engine_Mps) M/s " +
            "($($worst.Speedup)x vs $baseName).",
        "",
        "> A venue must survive its worst regime, so the worst-case row is the definitional number."
    )
    # WriteAllText (UTF-8 without BOM) - Set-Content -Encoding UTF8 prepends a BOM.
    [System.IO.File]::WriteAllText($md, ($md_lines -join "`n"))

    Write-Host "CSV report:      $csv" -ForegroundColor Green
    Write-Host "Markdown report: $md"  -ForegroundColor Green
}
finally { Pop-Location }
