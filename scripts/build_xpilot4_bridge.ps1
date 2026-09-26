[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$SkipProbe
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$dependencyRoot = Join-Path $repoRoot 'build\_deps\xpilot-plugin-sdk'
$sdkCommit = '3f3472f45c51717b90d91adb80114874bad7e8d1'
$sdkProject = Join-Path $dependencyRoot 'xPilot.PluginSdk.csproj'
$bridgeProject = Join-Path $repoRoot 'integrations\xpilot4_bridge\XVatsim.XPilot4Bridge.csproj'
$probeProject = Join-Path $repoRoot 'tools\xpilot4_bridge_probe\XVatsim.XPilot4Bridge.Probe.csproj'

if (-not (Test-Path -LiteralPath (Join-Path $dependencyRoot '.git'))) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dependencyRoot) | Out-Null
    git clone https://github.com/xpilot-project/plugin-sdk.git $dependencyRoot
}

git -C $dependencyRoot fetch --tags --prune
git -C $dependencyRoot checkout --detach $sdkCommit
if ((git -C $dependencyRoot rev-parse HEAD).Trim() -ne $sdkCommit) {
    throw 'Pinned xPilot Plugin SDK checkout does not match the required commit.'
}

$source = Get-Content -LiteralPath (Join-Path $repoRoot 'integrations\xpilot4_bridge\XVatsimXPilot4Bridge.cs') -Raw
$forbiddenCalls = @(
    '.RequestConnect(', '.RequestConnectAsObserver(', '.RequestConnectAsTowerView(',
    '.RequestDisconnect(', '.RequestMetar(', '.RequestAtis(', '.SendPrivateMessage(',
    '.SendRadioMessage(', '.PostDebugMessage(', '.SetModeC(', '.SquawkIdent(', '.SetPtt('
)
foreach ($forbidden in $forbiddenCalls) {
    if ($source.Contains($forbidden, [StringComparison]::Ordinal)) {
        throw "Forbidden xPilot write/control call found: $forbidden"
    }
}

dotnet build $bridgeProject -c $Configuration -p:XPilotPluginSdkProject=$sdkProject
if ($LASTEXITCODE -ne 0) {
    throw "xPilot 4 companion build failed with exit code $LASTEXITCODE."
}
dotnet build $probeProject -c $Configuration -p:XPilotPluginSdkProject=$sdkProject
if ($LASTEXITCODE -ne 0) {
    throw "xPilot 4 companion probe build failed with exit code $LASTEXITCODE."
}

$bridgeOutput = Join-Path $repoRoot "integrations\xpilot4_bridge\bin\$Configuration\net10.0"
if (Test-Path -LiteralPath (Join-Path $bridgeOutput 'xPilot.PluginSdk.dll')) {
    throw 'Companion output incorrectly contains xPilot.PluginSdk.dll.'
}
if (-not (Test-Path -LiteralPath (Join-Path $bridgeOutput 'XVatsim.XPilot4Bridge.dll'))) {
    throw 'Companion DLL was not produced.'
}

if (-not $SkipProbe) {
    dotnet run --project $probeProject -c $Configuration --no-build -p:XPilotPluginSdkProject=$sdkProject
    if ($LASTEXITCODE -ne 0) {
        throw "xPilot 4 companion probe failed with exit code $LASTEXITCODE."
    }
}

Write-Output "XPILOT4_BRIDGE_BUILD_PASSED configuration=$Configuration sdk=$sdkCommit sdkShipped=0"
