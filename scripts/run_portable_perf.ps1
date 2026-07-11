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
function Find-WorkingPython {
    $candidates = @(
        @{ Name = "py";      Prefix = @("-3") },
        @{ Name = "python";  Prefix = @() },
        @{ Name = "python3"; Prefix = @() }
    )
    foreach ($candidate in $candidates) {
        $command = Get-Command $candidate.Name -ErrorAction SilentlyContinue
        if (-not $command) { continue }
        try {
            $candidatePrefix = @($candidate.Prefix)
            & $command.Source $candidatePrefix -c "import sys; assert sys.version_info.major == 3" 2>$null
            if ($LASTEXITCODE -eq 0) {
                return @{ Command = $command; Prefix = $candidate.Prefix }
            }
        } catch {
            # App-execution aliases can be discoverable but fail when launched.
        }
    }
    return $null
}

$pythonChoice = Find-WorkingPython
if (-not $pythonChoice) {
    throw "Working Python 3 not found. The Windows 'py -3' launcher, 'python', and 'python3' were all tried."
}
$python = $pythonChoice.Command
$prefix = $pythonChoice.Prefix
$argsList = $prefix + @($runner, "--build-dir", $BuildDir, "--reps", $Reps)
$argsList += @("--scenarios", $Scenarios)
if ($Label) { $argsList += @("--label", $Label) }
if ($OutputDir) { $argsList += @("--output-dir", $OutputDir) }
if ($LiquibookFirst) { $argsList += "--liquibook-first" }
& $python.Source @argsList
exit $LASTEXITCODE
