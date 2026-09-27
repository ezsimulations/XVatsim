param(
    [string]$Version = "2.1.0",
    [string]$PlatformName = "Windows_XP12",
    [string]$BuildRoot = "build\v2.1.0-release",
    [string]$ReleaseOutputRoot = "releases",
    [string]$ManagerPath = "manager\artifacts\XVatsimManager.exe",
    [string]$ExpectedManagerSHA256 = "8AA67D8039742435B44F695E2C8B5DCB2751DE21C36612AE6C44A7629D3D52E0",
    [string]$ExpectedPluginSHA256 = "0D4ACC2A99309EE3ACC640798C286144044E57595E93AD8F7364485A65CBAEEF"
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
        "WINDOWS_SMARTSCREEN.txt",
        "SHA256SUMS.txt",
        "XVatsimManager.exe",
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

    $actual = Get-ChildItem -LiteralPath $PackageRoot -Recurse -Force -File |
        ForEach-Object {
            $_.FullName.Substring($PackageRoot.Length).TrimStart('\')
        }
    $unexpected = $actual | Where-Object { $_ -notin $required }
    if ($unexpected) {
        $names = $unexpected -join "`n"
        throw "Unexpected files found in freeware package:`n$names"
    }
    if ($actual.Count -ne $required.Count) {
        throw "Freeware package must contain exactly $($required.Count) files; found $($actual.Count)."
    }
}

$repoRoot = Resolve-RepoRoot
$releaseName = "XVatsim_$($Version)_Freeware_$PlatformName"
$buildRootPath = if ([IO.Path]::IsPathRooted($BuildRoot)) {
    [IO.Path]::GetFullPath($BuildRoot)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $BuildRoot))
}
$releaseOutputRootPath = if ([IO.Path]::IsPathRooted($ReleaseOutputRoot)) {
    [IO.Path]::GetFullPath($ReleaseOutputRoot)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $ReleaseOutputRoot))
}
if (-not (Test-Path -LiteralPath $releaseOutputRootPath)) {
    New-Item -ItemType Directory -Path $releaseOutputRootPath | Out-Null
}
$releaseRoot = Join-Path $releaseOutputRootPath $releaseName
$payloadRoot = Join-Path $releaseRoot $releaseName
$zipPath = Join-Path $releaseOutputRootPath "$releaseName.zip"

$builtXpl = Join-Path $buildRootPath "dist\XVatsim\win_x64\XVatsim.xpl"
$transitionAudio = Join-Path $repoRoot "assets\audio\ui_transition.mp3"
$authorityRegistry = Join-Path $repoRoot "assets\source_data\authority_source_registry.json"
$userGuide = Join-Path $repoRoot "docs\user_guide\XVatsim_User_Guide.pdf"
$managerSource = if ([IO.Path]::IsPathRooted($ManagerPath)) {
    [IO.Path]::GetFullPath($ManagerPath)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $ManagerPath))
}

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
Copy-RequiredFile -Source $managerSource -Destination (Join-Path $payloadRoot "XVatsimManager.exe")

$packagedManagerHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $payloadRoot "XVatsimManager.exe")).Hash
if ($ExpectedManagerSHA256 -and $packagedManagerHash -ne $ExpectedManagerSHA256) {
    throw "Manager hash does not match the approved release artifact. Expected $ExpectedManagerSHA256; found $packagedManagerHash."
}

$packagedPluginHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $pluginRoot "XVatsim.xpl")).Hash
if ($ExpectedPluginSHA256 -and $packagedPluginHash -ne $ExpectedPluginSHA256) {
    throw "Plugin hash does not match the approved release artifact. Expected $ExpectedPluginSHA256; found $packagedPluginHash."
}

$readme = @"
XVatsim Freeware
Version: $Version
Platform: Windows / X-Plane 12 / xPilot

XVatsim is a freeware companion plugin for xPilot in X-Plane 12. It provides a clean, route-aware VATSIM frequency overlay focused on IFR flight-plan operations.

This package includes the standalone XVatsim Manager for guided installation,
update, repair, verification, and automatic rollback. The Manager also installs
the read-only companion required for complete xPilot 4 support. xPilot 3 keeps
using XVatsim's established legacy integration.

What XVatsim does:
- Shows relevant VATSIM frequencies for the current flight phase.
- Supports departure, enroute, and arrival frequency awareness.
- Uses VATSIM, route, airport, transmitter, authority, and US vNAS sector facts to strengthen terminal-controller selection.
- Displays COM1, COM2, TX, RX, MODE C, and Standby Assist state.
- Provides Brain-owned METAR and VATSIM ATIS information drawers.
- Monitors xPilot's network log for incoming direct private and PDC/ACARS messages and stores them newest first in the PDC drawer.
- Can recover the current flight after an xPilot disconnect/reconnect.
- Can manually check whether the installed XVatsim version is current.
- Can show CTAF or UNICOM fallback when controlled airport service is unavailable.

Recommended installation:
1. Close X-Plane 12 and xPilot.
2. Extract this zip file.
3. Run XVatsimManager.exe.
4. If Windows SmartScreen appears, select More info, confirm the file name is
   XVatsimManager.exe, then select Run anyway. The Manager is intentionally
   distributed as unsigned freeware.
5. Review the X-Plane and xPilot folders found by the Manager. Choose a folder
   manually only if the Manager asks.
6. Select Install, Update, or Repair and wait for the success message.
7. Start X-Plane 12 and xPilot, then connect to VATSIM.

Manual X-Plane-only fallback:
1. Close X-Plane 12.
2. Copy the included Resources folder into your X-Plane 12 root folder.
3. Confirm this file exists:
   X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl
4. Start X-Plane 12 and xPilot.

The manual fallback does not install the xPilot 4 companion. xPilot 4 pilots
should use XVatsimManager.exe for complete Version $Version support.

Read XVatsim_User_Guide.pdf for complete setup, menu, keyboard-command, and troubleshooting instructions.

Support:
ezsimulations@gmail.com

For bug reports, include your callsign, route, what xPilot showed, what XVatsim showed, screenshots if possible, and these files:
- X-Plane 12\Resources\plugins\XVatsim\logs\xvatsim_diagnostics_YYYY_MM_DD.log
- X-Plane 12\Log.txt

XVatsim is for home flight simulation only. It is not approved for real-world aviation, navigation, dispatch, flight planning, or air traffic control use.
"@

$quickStart = @"
XVatsim Freeware Quick Start
Version: $Version

Recommended installation:
1. Close X-Plane 12 and xPilot.
2. Extract the zip file.
3. Run XVatsimManager.exe.
4. If Windows SmartScreen appears, select More info, confirm the file name,
   and select Run anyway.
5. Confirm the detected X-Plane and xPilot folders.
6. Select Install, Update, or Repair.
7. Wait for the success message, then start X-Plane 12 and xPilot.

The included Resources folder is a manual X-Plane-only fallback. It does not
install the xPilot 4 companion.

Basic use:
- XVatsim stays hidden until xPilot connects.
- Use Plugins > XVatsim > Auto Display for normal automatic wake/sleep behavior.
- Use Plugins > XVatsim > Open Display to force the overlay open.
- Use Plugins > XVatsim > Close Display to force it closed.
- Use Plugins > XVatsim > Recover Current Flight after reconnecting xPilot during an active flight.
- Use Plugins > XVatsim > Reset XVatsim Session when starting a new flight.
- Use Plugins > XVatsim > Check for Updates to manually verify the installed version.

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

Version 2.1.0 Manager and xPilot 4 update:
- Adds the standalone XVatsim Manager for guided first installation, update,
  repair, SHA-256 verification, transaction backup, and automatic rollback.
- Adds a read-only xPilot 4 SDK companion that reports facts and events to
  XVatsim while preserving the Brain as the only decision maker.
- Keeps xPilot 3 on the proven legacy integration with no companion DLL.
- Accepts the approved xPilot 4 SDK 0.1.x family and refuses an incompatible
  future SDK before subscribing to events.
- Recovers retained xPilot 4 PDC/ACARS information at startup and wraps long
  message text in the XVatsim drawer.
- Derives useful flight rules from METAR ceiling and visibility evidence when
  source classification is stale or unknown, and advances the received-age
  display after download.

Version 2.0.2 controller and message update:
- Keeps controller selection in the Brain with simple yes, no, and neutral evidence votes from VATSIM, VATSpy, airport distance, route geometry, coverage extensions, and US vNAS data when available.
- Recognizes Melbourne controller callsign variations and declared Australian Center coverage extensions without allowing a worker to hide a candidate.
- Monitors xPilot's network log every five seconds for incoming direct private and PDC/ACARS messages while excluding public radio, broadcast, server, outgoing, and other-callsign traffic.
- Stores PDC/private messages newest first and returns the PDC ORB from amber NEW to cyan IDLE after the pilot leaves the opened drawer.
- Replaces routine per-callback diagnostic records with one-minute timing summaries and rate-limited performance outliers.
- Preserves route-polygon colors, controller distance, Standby Assist, METAR, ATIS, CTAF, and the V2.0.1 settled performance path.

Version 2.0.1 performance maintenance update:
- Reduces steady X-Plane flight-loop work after the active flight context has settled.
- Runs the full operational refresh immediately when relevant operational state changes.
- Retains a one-second safety refresh while unchanged callbacks use the lightweight settled fast path.
- Avoids repeated inactive vNAS scans and unnecessary diagnostic formatting.

Version 2.0.0 feature foundation:
- Adds three Brain-owned information ORBs and drawers: METAR, VATSIM ATIS, and a one-shot xPilot waiting-message PDC snapshot.
- Strengthens US Approach/Departure selection with vNAS sector evidence while keeping the Brain as the sole decision owner.
- Evaluates terminal-controller transmitter distance from the departure or arrival airport instead of the aircraft position.
- Preserves the aircraft-distance gate for Center presentation within 250 nautical miles.
- Prevents unchanged ATIS content from repeatedly returning to unread when VATSIM republishes the same information.
- Moves route and authority preparation off the X-Plane flight loop.
- Includes IFR/VFR mode selection, diversion handling, flight recovery, update notifications, and exact route-polygon crossing fixes.

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
- Public radio-chat display or AUTO_ATC card presentation.
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
- X-Plane 12\Resources\plugins\XVatsim\logs\xvatsim_diagnostics_YYYY_MM_DD.log
- X-Plane 12\Log.txt

Diagnostic logs are generated locally while XVatsim runs. They are not included in this package.
"@

$smartScreen = @"
XVatsim Manager And Windows SmartScreen

XVatsimManager.exe is intentionally distributed as unsigned freeware. Windows
may display a SmartScreen message because the executable does not have a paid
publisher certificate.

To run the Manager downloaded from the official XVatsim X-Plane.org page or
GitHub release:
1. Select More info on the Windows protected your PC message.
2. Confirm the application name is XVatsimManager.exe.
3. Select Run anyway.

You can verify the exact downloaded files with SHA256SUMS.txt in this folder.
The official release page also publishes the complete ZIP and Manager SHA-256
values.

If you do not want to run the Manager, the Resources folder can be copied into
the X-Plane 12 root folder manually. That fallback does not install the xPilot
4 companion.
"@

Write-Utf8TextFile -Path (Join-Path $payloadRoot "README.txt") -Text $readme
Write-Utf8TextFile -Path (Join-Path $payloadRoot "QUICK_START.txt") -Text $quickStart
Write-Utf8TextFile -Path (Join-Path $payloadRoot "FREEWARE_LICENSE.txt") -Text $license
Write-Utf8TextFile -Path (Join-Path $payloadRoot "CHANGELOG.txt") -Text $changelog
Write-Utf8TextFile -Path (Join-Path $payloadRoot "SUPPORT.txt") -Text $support
Write-Utf8TextFile -Path (Join-Path $payloadRoot "WINDOWS_SMARTSCREEN.txt") -Text $smartScreen

$checksumLines = Get-ChildItem -LiteralPath $payloadRoot -Recurse -Force -File |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($payloadRoot.Length).TrimStart('\').Replace('\', '/')
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash
        "$hash *$relative"
    }
Write-Utf8TextFile -Path (Join-Path $payloadRoot "SHA256SUMS.txt") -Text ($checksumLines -join "`n")

Assert-CleanPackage -PackageRoot $payloadRoot

Compress-Archive -LiteralPath $payloadRoot -DestinationPath $zipPath -CompressionLevel Optimal

$zipHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $zipPath).Hash
$zipSize = (Get-Item -LiteralPath $zipPath).Length

[pscustomobject]@{
    Version = $Version
    PackageRoot = $payloadRoot
    ZipPath = $zipPath
    ZipSizeBytes = $zipSize
    ZipSHA256 = $zipHash
    PackagedManagerSHA256 = $packagedManagerHash
    PackagedPluginSHA256 = $packagedPluginHash
    PackageFileCount = (Get-ChildItem -LiteralPath $payloadRoot -Recurse -Force -File).Count
}
