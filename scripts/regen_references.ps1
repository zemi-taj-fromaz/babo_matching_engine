# regen_references.ps1 - regenerate the x86 correctness references from liquibook,
# persist them to the source tree, and verify babo matches on all five scenarios.
#
# Why this exists:
#   * --write-reference already WRITES reference/correctness_hash.txt for you
#     (per scenario, accumulating). This script just loops the five scenarios.
#   * The harness reads that file from the CWD (the build dir), but the harness
#     POST_BUILD step re-copies the SOURCE reference over it on every build - so
#     regenerated hashes are lost on the next rebuild unless copied back to the
#     source tree. This script does that copy so they persist (and so git sees them).
#
# Usage (from anywhere):
#   powershell -ExecutionPolicy Bypass -File scripts\regen_references.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\regen_references.ps1 -Count 100000
#   powershell -ExecutionPolicy Bypass -File scripts\regen_references.ps1 -BenchDir <path-to-harness-dir>
#
param(
    [string]$BenchDir = "",   # auto-detected when empty (see below)
    [int]$Count = 1000000
)
$ErrorActionPreference = "Stop"
$scenarios = "static","normal","swing-25","swing-40","flash-crash"
$repoRoot  = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$srcRef    = (Join-Path $repoRoot "benchmark\reference\correctness_hash.txt")

# Locate the harness with NO hardcoded build-dir name:
#   1. -BenchDir if you passed it
#   2. the current directory, if you are running from where harness.exe lives
#   3. the newest cmake-build-*/benchmark/harness.exe under the repo
if (-not $BenchDir) {
    if (Test-Path (Join-Path $PWD.Path "harness.exe")) {
        $BenchDir = $PWD.Path
    } else {
        $hit = Get-ChildItem $repoRoot -Directory -Filter "cmake-build-*" -ErrorAction SilentlyContinue |
               ForEach-Object { Join-Path $_.FullName "benchmark\harness.exe" } |
               Where-Object { Test-Path $_ } | Get-Item |
               Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($hit) { $BenchDir = $hit.DirectoryName }
        else { throw "harness.exe not found - cd to its directory, or pass -BenchDir <dir>." }
    }
}
$BenchDir = (Resolve-Path $BenchDir).Path
if (-not (Test-Path (Join-Path $BenchDir "harness.exe"))) {
    throw "harness.exe not found in $BenchDir - build it first, e.g.:`n" +
          "  cmake --build <build-dir> --target harness generator liquibook_adapter babobook_adapter"
}
Write-Host "Using harness in: $BenchDir" -ForegroundColor DarkGray

Push-Location $BenchDir
try {
    # --baseline liquibook loads .\liquibook_adapter.dll from the cwd. The
    # POST_BUILD stages it next to harness.exe; copy defensively just in case.
    if (Test-Path adapters\liquibook_adapter.dll) {
        Copy-Item adapters\liquibook_adapter.dll .\liquibook_adapter.dll -Force
    }

    Write-Host "== Writing references (count=$Count) from liquibook ==" -ForegroundColor Cyan
    foreach ($s in $scenarios) {
        .\harness.exe --baseline liquibook --scenario $s --count $Count --write-reference | Out-Null
        Write-Host "  wrote $s"
    }

    # Persist to the source tree so a rebuild's POST_BUILD copy does not revert it.
    Copy-Item .\reference\correctness_hash.txt $srcRef -Force
    Write-Host "Persisted references -> $srcRef" -ForegroundColor Green

    Write-Host "`n== Verifying babo matches ==" -ForegroundColor Cyan
    if (Test-Path adapters\babobook_adapter.dll) {
        Copy-Item adapters\babobook_adapter.dll .\babobook_adapter.dll -Force
    }
    if (-not (Test-Path .\babobook_adapter.dll)) {
        Write-Host "  (babobook_adapter.dll not built - skipping babo verification)" -ForegroundColor Yellow
        return
    }
    foreach ($s in $scenarios) {
        $out     = .\harness.exe --engine .\babobook_adapter.dll --scenario $s --count $Count --mode perf
        $status  = (($out | Select-String "Status:")  -replace '.*Status:\s*','').Trim()
        $verdict = (($out | Select-String "Verdict:") -replace '.*Verdict:\s*','').Trim()
        $color   = if ($status -eq "PASS") { "Green" } else { "Red" }
        Write-Host ("  {0,-12} {1,-14} {2}" -f $s, $status, $verdict) -ForegroundColor $color
    }
}
finally { Pop-Location }
