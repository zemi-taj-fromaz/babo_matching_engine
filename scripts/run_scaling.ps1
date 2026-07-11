param(
    [Parameter(Mandatory=$true)][string]$BuildDir,
    [int]$Levels = 64,
    [int]$Reps = 3,
    [string]$Sizes = "",
    [int]$Max = 0,
    [string]$Label = "",
    [string]$OutputDir = ""
)
$ErrorActionPreference = "Stop"
$runner = Join-Path $PSScriptRoot "run_scaling.py"

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
$argsList = $prefix + @($runner, "--build-dir", $BuildDir, "--levels", $Levels, "--reps", $Reps)
if ($Sizes)     { $argsList += @("--sizes", $Sizes) }
if ($Max -gt 0) { $argsList += @("--max", $Max) }
if ($Label)     { $argsList += @("--label", $Label) }
if ($OutputDir) { $argsList += @("--output-dir", $OutputDir) }
& $python.Source @argsList
exit $LASTEXITCODE
