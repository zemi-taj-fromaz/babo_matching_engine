param(
    [Parameter(Mandatory=$true)][string]$BuildDir,
    [int]$Reps = 100,
    [string]$Label = "",
    [string]$OutputDir = "",
    [string]$Scenarios = "static,normal,swing25,flash_crash_40,flash_crash_60",
    [switch]$LiquibookFirst
)
$ErrorActionPreference = "Stop"
$runner = Join-Path $PSScriptRoot "run_portable_perf.py"
$python = Get-Command python3 -ErrorAction SilentlyContinue
$prefix = @()
if (-not $python) { $python = Get-Command python -ErrorAction SilentlyContinue }
if (-not $python) { $python = Get-Command py -ErrorAction SilentlyContinue; if ($python) { $prefix = @("-3") } }
if (-not $python) { throw "Python 3 not found. Install Python 3 or put it on PATH." }
$argsList = $prefix + @($runner, "--build-dir", $BuildDir, "--reps", $Reps)
$argsList += @("--scenarios", $Scenarios)
if ($Label) { $argsList += @("--label", $Label) }
if ($OutputDir) { $argsList += @("--output-dir", $OutputDir) }
if ($LiquibookFirst) { $argsList += "--liquibook-first" }
& $python.Source @argsList
exit $LASTEXITCODE
