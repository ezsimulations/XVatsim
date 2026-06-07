param(
    [string]$Version = "1.0.2",
    [string]$PlatformName = "Windows_XP12"
)

$ErrorActionPreference = "Stop"

function Resolve-RepoRoot {
    $scriptDir = Split-Path -Parent $PSCommandPath
    return (Resolve-Path (Join-Path $scriptDir "..\..")).Path
}

function Copy-RequiredFile {
    param(
        [string]$Source,
        [string]$Destination
    )

    if (-not (Test-Path -LiteralPath $Source)) {
        throw "Required release input is missing: $Source"
    }

    $destDir = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $destDir)) {
        New-Item -ItemType Directory -Path $destDir | Out-Null
    }

    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Write-Utf8TextFile {
    param(
        [string]$Path,
        [string]$Text
    )

    $dir = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir | Out-Null
    }

    Set-Content -LiteralPath $Path -Value $Text.TrimStart() -Encoding UTF8
}

function Assert-CleanPackage {
    param([string]$PackageRoot)

    $forbidden = Get-ChildItem -LiteralPath $PackageRoot -Recurse -Force -File |
        Where-Object {
            $_.Name -match '(?i)\.(pdb|lib|exp|tmp|log)$' -or
            $_.Name -match '(?i)^desktop\.ini$' -or
            $_.FullName -match '(?i)[\\/](Debug|Release|RelWithDebInfo)[\\/]'
        }

    if ($forbidden) {
        $names = ($forbidden | ForEach-Object { $_.FullName }) -join "`n"
        throw "Forbidden generated/debug artifacts found in freeware package:`n$names"
    }

    $required = @(
        "README.txt",
        "QUICK_START.txt",
        "FREEWARE_LICENSE.txt",
        "CHANGELOG.txt",
        "SUPPORT.txt",
        "XVatsim_User_Guide.pdf",
        "Resources\plugins\XVatsim\win_x64\XVatsim.xpl",
        "Resources\plugins\XVatsim\win_x64\ui_transition.mp3",
        "Resources\plugins\XVatsim\win_x64\authority_source_registry.json"
    )

    foreach ($relative in $required) {
        $path = Join-Path $PackageRoot $relative
        if (-not (Test-Path -LiteralPath $path)) {
            throw "Expected package file is missing: $relative"
        }
    }
}

$repoRoot = Resolve-RepoRoot
$releaseName = "XVatsim_$($Version)_Freeware_$PlatformName"
$releaseRoot = Join-Path $repoRoot "releases\$releaseName"
$payloadRoot = Join-Path $releaseRoot $releaseName
$zipPath = Join-Path $repoRoot "releases\$releaseName.zip"

$builtXpl = Join-Path $repoRoot "build\dist\XVatsim\win_x64\XVatsim.xpl"
$transitionAudio = Join-Path $repoRoot "assets\audio\ui_transition.mp3"
$authorityRegistry = Join-Path $repoRoot "assets\source_data\authority_source_registry.json"
$userGuide = Join-Path $repoRoot "docs\user_guide\XVatsim_User_Guide.pdf"

if (Test-Path -LiteralPath $releaseRoot) {
    Remove-Item -LiteralPath $releaseRoot -Recurse -Force
}
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

$pluginRoot = Join-Path $payloadRoot "Resources\plugins\XVatsim\win_x64"
Copy-RequiredFile -Source $builtXpl -Destination (Join-Path $pluginRoot "XVatsim.xpl")
Copy-RequiredFile -Source $transitionAudio -Destination (Join-Path $pluginRoot "ui_transition.mp3")
Copy-RequiredFile -Source $authorityRegistry -Destination (Join-Path $pluginRoot "authority_source_registry.json")
Copy-RequiredFile -Source $userGuide -Destination (Join-Path $payloadRoot "XVatsim_User_Guide.pdf")

$readme = @"
XVatsim Freeware
Version: $Version
Platform: Windows / X-Plane 12 / xPilot

XVatsim is a freeware companion plugin for xPilot in X-Plane 12. It provides a clean, route-aware VATSIM frequency overlay focused on IFR flight-plan operations.

What XVatsim does:
- Shows relevant VATSIM frequencies for the current flight phase.
- Supports departure, enroute, and arrival frequency awareness.
- Displays COM1, COM2, TX, RX, MODE C, and Standby Assist state.
- Can recover the current flight after an xPilot disconnect/reconnect.
- Can show CTAF or UNICOM fallback when controlled airport service is unavailable.

Install:
1. Close X-Plane 12.
2. Extract this zip file.
3. Copy the included Resources folder into your X-Plane 12 root folder.
4. Confirm this file exists:
   X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl
5. Start X-Plane 12.
6. Start xPilot and connect to VATSIM.

Read XVatsim_User_Guide.pdf for complete setup, menu, keyboard-command, and troubleshooting instructions.

Support:
ezsimulations@gmail.com

For bug reports, include your callsign, route, what xPilot showed, what XVatsim showed, screenshots if possible, and these files:
- X-Plane 12\Resources\plugins\XVatsim\logs\xvatsim_diagnostics.log
- X-Plane 12\Log.txt

XVatsim is for home flight simulation only. It is not approved for real-world aviation, navigation, dispatch, flight planning, or air traffic control use.
"@

$quickStart = @"
XVatsim Freeware Quick Start
Version: $Version

Install:
1. Close X-Plane 12.
2. Extract the zip file.
3. Copy the included Resources folder into your X-Plane 12 root folder.
4. Confirm the plugin exists at:
   X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl
5. Start X-Plane 12.
6. Start xPilot and connect to VATSIM.

Basic use:
- XVatsim stays hidden until xPilot connects.
- Use Plugins > XVatsim > Auto Display for normal automatic wake/sleep behavior.
- Use Plugins > XVatsim > Open Display to force the overlay open.
- Use Plugins > XVatsim > Close Display to force it closed.
- Use Plugins > XVatsim > Recover Current Flight after reconnecting xPilot during an active flight.
- Use Plugins > XVatsim > Reset XVatsim Session when starting a new flight.

Keyboard commands:
Open X-Plane Settings > Keyboard and search for xvatsim.
See XVatsim_User_Guide.pdf for all command names and menu functions.
"@

$license = @"
XVatsim Freeware License
Version: $Version

XVatsim is freeware. You may use it for personal home flight simulation without payment.

You may share the original XVatsim freeware package as long as the package remains intact and unmodified, including the plugin, documentation, audio file, authority registry, and this license.

You may not sell XVatsim, charge for access to XVatsim, claim XVatsim as your own work, remove author/support information, repackage it in a misleading way, or bundle it with another commercial product without written permission from EZ SIMULATIONS.

XVatsim is not open-source unless source code is released separately under an explicit open-source license. This freeware license does not grant permission to reverse engineer, decompile, modify, or create derivative works from the plugin except where applicable law expressly permits it.

XVatsim is provided as-is, without warranty. Use it at your own risk. It is for home flight simulation only and is not approved for real-world aviation, navigation, dispatch, flight planning, or air traffic control use.

Support contact:
ezsimulations@gmail.com
"@

$changelog = @"
XVatsim Freeware Changelog
Version: $Version

Patch release:
- Fixes arrival APP/DEP/TRACON authority matching when SimAware terminal boundaries do not explicitly separate APP and DEP.
- Keeps the brain-owned relevance contract: radio-board candidates are evaluated against endpoint terminal authority facts, then accepted or rejected with diagnostic reasons.
- Adds regression coverage for KEWR arrival EWR_DEP and KPVD arrival PVD_APP terminal authority cases.

Current scope:
- Windows only.
- X-Plane 12 only.
- xPilot required.
- IFR flight-plan workflow.

Not included in this release:
- Mac or Linux support.
- X-Plane 11 support.
- Dedicated VFR workflow.
- SimBrief import.
- Navigraph AIRAC import.
- Private-message, PDC, or AUTO_ATC card presentation.
"@

$support = @"
XVatsim Support

Support email:
ezsimulations@gmail.com

When reporting a bug, include:
- Callsign.
- Departure and arrival airports.
- Filed route if available.
- What xPilot showed.
- What XVatsim showed.
- Screenshots if possible.
- Whether xPilot disconnected/reconnected.
- Whether Standby Assist was on or off.

Useful log files:
- X-Plane 12\Resources\plugins\XVatsim\logs\xvatsim_diagnostics.log
- X-Plane 12\Log.txt

Diagnostic logs are generated locally while XVatsim runs. They are not included in this package.
"@

Write-Utf8TextFile -Path (Join-Path $payloadRoot "README.txt") -Text $readme
Write-Utf8TextFile -Path (Join-Path $payloadRoot "QUICK_START.txt") -Text $quickStart
Write-Utf8TextFile -Path (Join-Path $payloadRoot "FREEWARE_LICENSE.txt") -Text $license
Write-Utf8TextFile -Path (Join-Path $payloadRoot "CHANGELOG.txt") -Text $changelog
Write-Utf8TextFile -Path (Join-Path $payloadRoot "SUPPORT.txt") -Text $support

Assert-CleanPackage -PackageRoot $payloadRoot

Compress-Archive -LiteralPath $payloadRoot -DestinationPath $zipPath -CompressionLevel Optimal

$xplHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $pluginRoot "XVatsim.xpl")).Hash
$zipHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $zipPath).Hash

[pscustomobject]@{
    Version = $Version
    PackageRoot = $payloadRoot
    ZipPath = $zipPath
    ZipSHA256 = $zipHash
    PackagedPluginSHA256 = $xplHash
}
