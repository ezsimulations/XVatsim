param(
    [Parameter(Mandatory = $true)]
    [string]$FmsPath,
    [string]$LogPath = "C:\X-Plane 12\Resources\plugins\XVatsim\logs\xvatsim_diagnostics_2026_10_28.log",
    [string]$CaptureName,
    [string]$SnapshotPath,
    [string]$OutputName = "experiment",
    [switch]$RefreshSources
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$bundledPython = "C:\Users\DARRON\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
$python = if (Test-Path -LiteralPath $bundledPython) { $bundledPython } else { "python" }

$arguments = @(
    "-m", "engineer_test.cli",
    "--fms", $FmsPath,
    "--log", $LogPath,
    "--output", (Join-Path $root "output\$OutputName"),
    "--cache", (Join-Path $root "cache")
)

if ($CaptureName) {
    $arguments += @("--capture", $CaptureName)
}
if ($SnapshotPath) {
    $arguments += @("--snapshot", $SnapshotPath)
}
if ($RefreshSources) {
    $arguments += "--refresh-sources"
}

$env:PYTHONPATH = Join-Path $root "src"
& $python @arguments
if ($LASTEXITCODE -ne 0) {
    throw "EngineerTest failed with exit code $LASTEXITCODE"
}

