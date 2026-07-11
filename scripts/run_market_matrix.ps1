param(
    [Parameter(Mandatory=$true)][string]$BuildDir,
    [string]$Scenarios = "",
    [string]$Counts = "",
    [int]$Reps = 10,
    [ValidateSet("peak","median","mean")][string]$Metric = "peak",
    [string]$Label = "",
    [string]$OutputDir = ""
)
$ErrorActionPreference = "Stop"
$runner = Join-Path $PSScriptRoot "run_market_matrix.py"

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
            if ($LASTEXITCODE -eq 0) { return @{ Command = $command; Prefix = $candidate.Prefix } }
        } catch { }
    }
    return $null
}

$pythonChoice = Find-WorkingPython
if (-not $pythonChoice) {
    throw "Working Python 3 not found. Tried the 'py -3' launcher, 'python', and 'python3'."
}
$python = $pythonChoice.Command
$prefix = $pythonChoice.Prefix
$argsList = $prefix + @($runner, "--build-dir", $BuildDir, "--reps", $Reps, "--metric", $Metric)
if ($Scenarios) { $argsList += @("--scenarios", $Scenarios) }
if ($Counts)    { $argsList += @("--counts", $Counts) }
if ($Label)     { $argsList += @("--label", $Label) }
if ($OutputDir) { $argsList += @("--output-dir", $OutputDir) }
& $python.Source @argsList
exit $LASTEXITCODE
