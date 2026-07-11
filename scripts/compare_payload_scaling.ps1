# Compare babobook and Liquibook as the generated input payload grows, then emit
# CSV, Markdown, and a dependency-free SVG graph.
#
# This is a workload-size experiment, not a resting-book-size experiment. With
# the canonical 95% cancel lifecycle, N NEW orders do not leave N resting orders.
# Use a separate prefill/depth sweep to isolate cancel complexity vs book size.
param(
    [Parameter(Mandatory=$true)][string]$Engine,
    [string]$Baseline = "adapters\liquibook_adapter.dll",
    [string]$BenchDir = "",
    [string]$Scenario = "normal",
    [int64[]]$Counts = @(1000, 10000, 100000, 1000000, 10000000),
    [int]$Reps = 3,
    [switch]$Include100M
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

if ($Include100M -and -not ($Counts -contains 100000000)) {
    $Counts += 100000000
}
$Counts = $Counts | Sort-Object -Unique
if ($Counts.Count -eq 0 -or ($Counts | Where-Object { $_ -lt 2 }).Count -gt 0) {
    throw "Every payload count must be at least 2."
}
if ($Reps -lt 1) { throw "Reps must be at least 1." }

if (-not $BenchDir) {
    $hit = Get-ChildItem $repoRoot -Directory -Filter "cmake-build-*" -ErrorAction SilentlyContinue |
           ForEach-Object { Join-Path $_.FullName "benchmark\harness.exe" } |
           Where-Object { Test-Path $_ } | Get-Item |
           Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $hit) { throw "harness.exe not found; pass -BenchDir <build\benchmark>." }
    $BenchDir = $hit.DirectoryName
}
$BenchDir = (Resolve-Path $BenchDir).Path
$harness = Join-Path $BenchDir "harness.exe"
if (-not (Test-Path $harness)) { throw "harness.exe not found in $BenchDir." }

$engName = [IO.Path]::GetFileNameWithoutExtension($Engine)
$baseName = [IO.Path]::GetFileNameWithoutExtension($Baseline)

function Stage-Adapter([string]$path) {
    $source = if ([IO.Path]::IsPathRooted($path)) { $path } else { Join-Path $BenchDir $path }
    if (-not (Test-Path $source)) { throw "Adapter not found: $source" }
    $leaf = Split-Path $source -Leaf
    $dest = Join-Path $BenchDir $leaf
    if ((Resolve-Path $source).Path -ne $dest) { Copy-Item $source $dest -Force }
    return ".\$leaf"
}

function Invoke-BestRun([string]$dll, [int64]$count) {
    $best = $null
    for ($rep = 1; $rep -le $Reps; $rep++) {
        $old = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        $text = (& $harness --engine $dll --scenario $Scenario --count $count --mode perf 2>&1 | Out-String)
        $ErrorActionPreference = $old
        # Non-canonical counts intentionally do not match the shipped 1M hash,
        # so harness may return INVALID/non-zero. This script validates the two
        # freshly computed hashes against each other instead.
        if ($text -notmatch 'Throughput:\s*([\d.]+)') { throw "No throughput in harness output.`n$text" }
        $throughput = [double]$Matches[1]
        $messages = if ($text -match 'Messages processed:\s*(\d+)') { [int64]$Matches[1] } else { 0 }
        $hash = if ($text -match 'Computed hash:\s*([0-9a-fA-F]{64})') { $Matches[1].ToLowerInvariant() } else { "" }
        $candidate = [pscustomobject]@{ Throughput=$throughput; Messages=$messages; Hash=$hash; Rep=$rep }
        if ($null -eq $best -or $candidate.Throughput -gt $best.Throughput) { $best = $candidate }
    }
    return $best
}

function Write-Svg([object[]]$rows, [string]$path) {
    $width=1000; $height=560; $left=90; $right=35; $top=45; $bottom=80
    $plotW=$width-$left-$right; $plotH=$height-$top-$bottom
    $maxY = [math]::Ceiling((($rows | ForEach-Object { [math]::Max($_.Liquibook_Mps,$_.babobook_Mps) } | Measure-Object -Maximum).Maximum) * 1.10)
    if ($maxY -le 0) { $maxY=1 }
    $minLog=[math]::Log10(($rows | Measure-Object -Property Input_NEW -Minimum).Minimum)
    $maxLog=[math]::Log10(($rows | Measure-Object -Property Input_NEW -Maximum).Maximum)
    if ($maxLog -eq $minLog) { $maxLog=$minLog+1 }
    function X([double]$n) { return $left + (([math]::Log10($n)-$minLog)/($maxLog-$minLog))*$plotW }
    function Y([double]$v) { return $top + $plotH - ($v/$maxY)*$plotH }
    $blue="#1769aa"; $orange="#d95f02"
    $svg = New-Object System.Collections.Generic.List[string]
    $svg.Add("<svg xmlns='http://www.w3.org/2000/svg' width='$width' height='$height' viewBox='0 0 $width $height'>")
    $svg.Add("<rect width='100%' height='100%' fill='white'/><style>text{font-family:Arial,sans-serif;fill:#222}.axis{stroke:#333}.grid{stroke:#ddd}.a{fill:none;stroke:$blue;stroke-width:3}.b{fill:none;stroke:$orange;stroke-width:3}</style>")
    $svg.Add("<text x='$($width/2)' y='24' text-anchor='middle' font-size='18' font-weight='bold'>Payload scaling - $Scenario</text>")
    for ($i=0; $i -le 5; $i++) {
        $v=$maxY*$i/5; $yy=Y $v
        $svg.Add("<line class='grid' x1='$left' y1='$yy' x2='$($left+$plotW)' y2='$yy'/><text x='$($left-10)' y='$($yy+4)' text-anchor='end' font-size='12'>$([math]::Round($v,1))</text>")
    }
    $svg.Add("<line class='axis' x1='$left' y1='$top' x2='$left' y2='$($top+$plotH)'/><line class='axis' x1='$left' y1='$($top+$plotH)' x2='$($left+$plotW)' y2='$($top+$plotH)'/>")
    $pa=@(); $pb=@()
    foreach ($row in $rows) {
        $xx=X $row.Input_NEW; $ya=Y $row.babobook_Mps; $yb=Y $row.Liquibook_Mps
        $pa += "$xx,$ya"; $pb += "$xx,$yb"
        $label = if ($row.Input_NEW -ge 1000000) { "$($row.Input_NEW/1000000)M" } elseif ($row.Input_NEW -ge 1000) { "$($row.Input_NEW/1000)K" } else { "$($row.Input_NEW)" }
        $svg.Add("<text x='$xx' y='$($top+$plotH+24)' text-anchor='middle' font-size='12'>$label</text>")
    }
    $pointsA = $pa -join ' '
    $pointsB = $pb -join ' '
    $svg.Add("<polyline class='a' points='$pointsA'/><polyline class='b' points='$pointsB'/>")
    foreach ($row in $rows) {
        $xx=X $row.Input_NEW; $ya=Y $row.babobook_Mps; $yb=Y $row.Liquibook_Mps
        $svg.Add("<circle cx='$xx' cy='$ya' r='4.5' fill='$blue'/><circle cx='$xx' cy='$yb' r='4.5' fill='$orange'/>")
    }
    $svg.Add("<text transform='translate(20 $($top+$plotH/2)) rotate(-90)' text-anchor='middle' font-size='14'>Throughput (M messages/s)</text><text x='$($left+$plotW/2)' y='$($height-16)' text-anchor='middle' font-size='14'>Generated NEW orders (log scale)</text>")
    $svg.Add("<line x1='$($width-250)' y1='48' x2='$($width-215)' y2='48' stroke='$blue' stroke-width='3'/><text x='$($width-205)' y='53' font-size='13'>babobook</text><line x1='$($width-130)' y1='48' x2='$($width-95)' y2='48' stroke='$orange' stroke-width='3'/><text x='$($width-85)' y='53' font-size='13'>Liquibook</text>")
    $svg.Add("</svg>")
    [IO.File]::WriteAllText($path, ($svg -join "`n"), [Text.UTF8Encoding]::new($false))
}

Push-Location $BenchDir
try {
    $engDll=Stage-Adapter $Engine
    $baseDll=Stage-Adapter $Baseline
    $rows=@()
    foreach ($count in $Counts) {
        if ($count -ge 100000000) {
            Write-Warning "100M is a huge materialized run: expect multi-GB disk use and tens of GB of peak memory."
        }
        Write-Host "Payload $count NEW orders ($Scenario): Liquibook..." -ForegroundColor Cyan
        $base=Invoke-BestRun $baseDll $count
        Write-Host "Payload $count NEW orders ($Scenario): babobook..." -ForegroundColor Cyan
        $eng=Invoke-BestRun $engDll $count
        $rows += [pscustomobject]@{
            Input_NEW=$count; Messages=$eng.Messages
            Liquibook_Mps=$base.Throughput; babobook_Mps=$eng.Throughput
            Speedup=[math]::Round($eng.Throughput/$base.Throughput,2)
            Hash_match=($base.Hash -ne "" -and $base.Hash -eq $eng.Hash)
        }
    }
    $stamp=Get-Date -Format "yyyyMMddTHHmmss"
    $outDir=Join-Path $BenchDir "results"; New-Item -ItemType Directory -Force $outDir | Out-Null
    $stem="payload_scaling_${Scenario}_$stamp"
    $csv=Join-Path $outDir "$stem.csv"; $md=Join-Path $outDir "$stem.md"; $svg=Join-Path $outDir "$stem.svg"
    $rows | Export-Csv -NoTypeInformation $csv
    $lines=@("# Payload scaling: babobook vs Liquibook","","Scenario: ``$Scenario``; best of $Reps runs per point.","","| Input NEW | Delivered messages | Liquibook M/s | babobook M/s | Speedup | Hash match |","|---:|---:|---:|---:|---:|:---:|")
    foreach ($row in $rows) { $lines += "| $($row.Input_NEW) | $($row.Messages) | $($row.Liquibook_Mps) | $($row.babobook_Mps) | $($row.Speedup)x | $($row.Hash_match) |" }
    $lines += @("","> Input size is not resting-book size: lifecycle cancellations keep the live book much smaller.","","![Payload scaling]($([IO.Path]::GetFileName($svg)))")
    [IO.File]::WriteAllText($md,($lines -join "`n"),[Text.UTF8Encoding]::new($false))
    Write-Svg $rows $svg
    $rows | Format-Table -AutoSize
    Write-Host "CSV: $csv`nMarkdown: $md`nGraph: $svg" -ForegroundColor Green
}
finally { Pop-Location }
