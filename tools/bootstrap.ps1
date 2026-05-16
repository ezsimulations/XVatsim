param(
    [string]$SdkRoot = "$PSScriptRoot\..\SDK"
)

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$resolvedSdk = (Resolve-Path $SdkRoot).Path
$buildDir = Join-Path $repoRoot 'build'

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $vsCmake = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (Test-Path $vsCmake) {
        $cmake = $vsCmake
    } else {
        throw 'CMake was not found on PATH and the expected Visual Studio CMake path does not exist.'
    }
}

& $cmake -S $repoRoot -B $buildDir -G "Visual Studio 18 2026" -A x64 -DXPLANE_SDK_ROOT=$resolvedSdk
