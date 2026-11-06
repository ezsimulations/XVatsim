$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$bundledPython = "C:\Users\DARRON\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
$python = if (Test-Path -LiteralPath $bundledPython) { $bundledPython } else { "python" }
$env:PYTHONPATH = Join-Path $root "src"
& $python -m unittest discover -s (Join-Path $root "tests") -v
if ($LASTEXITCODE -ne 0) {
    throw "EngineerTest unit tests failed with exit code $LASTEXITCODE"
}

