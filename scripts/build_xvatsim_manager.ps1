[CmdletBinding()]
param(
    [string]$ProductVersion = '2.1.0',
    [string]$PluginPath,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Fa-f0-9]{64}$')]
    [string]$ExpectedPluginSha256,
    [string]$BridgePath,
    [string]$ExpectedBridgeSha256,
    [switch]$SkipBridgeBuild
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$generatedRoot = Join-Path $repoRoot 'manager\generated'
$payloadRoot = Join-Path $generatedRoot 'payload'
$payloadArchive = Join-Path $generatedRoot 'payload.zip'
$artifactRoot = Join-Path $repoRoot 'manager\artifacts'
$managerProject = Join-Path $repoRoot 'manager\src\XVatsim.Manager\XVatsim.Manager.csproj'
$probeProject = Join-Path $repoRoot 'manager\tests\XVatsim.Manager.Probe\XVatsim.Manager.Probe.csproj'
if ([string]::IsNullOrWhiteSpace($PluginPath)) {
    $PluginPath = Join-Path $repoRoot 'build-release\dist\XVatsim\win_x64\XVatsim.xpl'
}
$audioPath = Join-Path $repoRoot 'assets\audio\ui_transition.mp3'
$registryPath = Join-Path $repoRoot 'assets\source_data\authority_source_registry.json'
if ([string]::IsNullOrWhiteSpace($BridgePath)) {
    $BridgePath = Join-Path $repoRoot 'integrations\xpilot4_bridge\bin\Release\net10.0\XVatsim.XPilot4Bridge.dll'
}

if (-not (Test-Path -LiteralPath $PluginPath)) {
    throw "Verified XVatsim build not found: $PluginPath"
}
$actualPluginHash = (Get-FileHash -LiteralPath $PluginPath -Algorithm SHA256).Hash
if ($actualPluginHash -ne $ExpectedPluginSha256.ToUpperInvariant()) {
    throw "The selected XVatsim.xpl does not match the expected tested SHA-256. Expected $ExpectedPluginSha256; found $actualPluginHash."
}
if (-not $SkipBridgeBuild) {
    & (Join-Path $repoRoot 'scripts\build_xpilot4_bridge.ps1') -Configuration Release
    if ($LASTEXITCODE -ne 0) { throw 'The xPilot 4 bridge build or probe failed.' }
}
if (-not (Test-Path -LiteralPath $BridgePath)) {
    throw "xPilot 4 bridge not found: $BridgePath"
}
if (-not [string]::IsNullOrWhiteSpace($ExpectedBridgeSha256) -and
    (Get-FileHash -LiteralPath $BridgePath -Algorithm SHA256).Hash -ne $ExpectedBridgeSha256) {
    throw 'The selected xPilot 4 bridge does not match the expected tested SHA-256.'
}

if (Test-Path -LiteralPath $generatedRoot) {
    Remove-Item -LiteralPath $generatedRoot -Recurse -Force
}
if (Test-Path -LiteralPath $artifactRoot) {
    Remove-Item -LiteralPath $artifactRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $payloadRoot -Force | Out-Null
New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null

$payloadSources = @(
    [pscustomobject]@{ RelativePath = 'Resources/plugins/XVatsim/win_x64/XVatsim.xpl'; Target = 'XPlane'; Source = $PluginPath },
    [pscustomobject]@{ RelativePath = 'Resources/plugins/XVatsim/win_x64/ui_transition.mp3'; Target = 'XPlane'; Source = $audioPath },
    [pscustomobject]@{ RelativePath = 'Resources/plugins/XVatsim/win_x64/authority_source_registry.json'; Target = 'XPlane'; Source = $registryPath },
    [pscustomobject]@{ RelativePath = 'xpilot4/XVatsim.XPilot4Bridge.dll'; Target = 'XPilot4'; Source = $BridgePath }
)

$manifestFiles = @()
foreach ($item in $payloadSources) {
    if (-not (Test-Path -LiteralPath $item.Source)) { throw "Payload source not found: $($item.Source)" }
    $destination = Join-Path $payloadRoot ($item.RelativePath.Replace('/', '\'))
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $item.Source -Destination $destination
    $copied = Get-Item -LiteralPath $destination
    $manifestFiles += [ordered]@{
        relativePath = $item.RelativePath
        target = $item.Target
        length = $copied.Length
        sha256 = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        required = $true
    }
}

$manifest = [ordered]@{
    schemaVersion = '2'
    productVersion = $ProductVersion
    createdUtc = [DateTimeOffset]::UtcNow.ToString('O')
    files = $manifestFiles
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $payloadRoot 'manifest.json') -Encoding utf8NoBOM

Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $payloadRoot,
    $payloadArchive,
    [System.IO.Compression.CompressionLevel]::Optimal,
    $false)

dotnet build (Join-Path $repoRoot 'manager\XVatsim.Manager.slnx') -c Release
if ($LASTEXITCODE -ne 0) { throw 'XVatsim Manager build failed.' }
dotnet run --project $probeProject -c Release --no-build
if ($LASTEXITCODE -ne 0) { throw 'XVatsim Manager offline probe failed.' }

dotnet publish $managerProject -c Release -r win-x64 --self-contained true `
    -p:PublishSingleFile=true `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -p:EnableCompressionInSingleFile=true `
    -p:DebugType=None `
    -p:DebugSymbols=false `
    -o $artifactRoot
if ($LASTEXITCODE -ne 0) { throw 'XVatsim Manager publish failed.' }

$exePath = Join-Path $artifactRoot 'XVatsimManager.exe'
if (-not (Test-Path -LiteralPath $exePath)) { throw 'The manager executable was not produced.' }
$extraFiles = @(Get-ChildItem -LiteralPath $artifactRoot -File | Where-Object Name -ne 'XVatsimManager.exe')
if ($extraFiles.Count -gt 0) {
    throw "The self-contained output contains unexpected files: $($extraFiles.Name -join ', ')"
}

$payloadHash = (Get-FileHash -LiteralPath $payloadArchive -Algorithm SHA256).Hash
$managerHash = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
Write-Output "XVATSIM_MANAGER_BUILD_PASSED version=$ProductVersion"
Write-Output "PAYLOAD_SHA256=$payloadHash"
Write-Output "MANAGER_SHA256=$managerHash"
Write-Output "MANAGER_PATH=$exePath"
