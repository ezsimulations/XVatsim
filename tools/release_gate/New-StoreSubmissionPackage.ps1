[CmdletBinding()]
param(
    [string]$WorkspaceRoot = "",
    [string]$Version = "1.0.0",
    [string]$KitDate = (Get-Date -Format "yyyy-MM-dd"),
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Resolve-WorkspaceRoot {
    if (-not [string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
        return (Resolve-Path -LiteralPath $WorkspaceRoot).Path
    }

    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).Path
}

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "== $Message =="
}

function Get-Sha256 {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Assert-File {
    param(
        [string]$Path,
        [string]$Label
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label missing: $Path"
    }
}

function Assert-UnderRoot {
    param(
        [string]$Root,
        [string]$Path,
        [string]$Label
    )

    $rootPath = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\') + '\'
    if (Test-Path -LiteralPath $Path) {
        $fullPath = (Resolve-Path -LiteralPath $Path).Path
    } else {
        $parent = Split-Path -Parent $Path
        $leaf = Split-Path -Leaf $Path
        $fullPath = (Resolve-Path -LiteralPath $parent).Path.TrimEnd('\') + '\' + $leaf
    }

    if (-not $fullPath.StartsWith($rootPath, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must stay under $rootPath. Resolved path: $fullPath"
    }
}

function Invoke-Checked {
    param(
        [string]$Executable,
        [string[]]$Arguments,
        [string]$Label
    )

    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

function Get-CMakePath {
    $knownCmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path -LiteralPath $knownCmake -PathType Leaf) {
        return $knownCmake
    }

    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if ($null -eq $cmakeCommand) {
        throw "CMake was not found. Install Visual Studio CMake tools or put cmake.exe on PATH."
    }

    return $cmakeCommand.Source
}

function Write-TextFile {
    param(
        [string]$Path,
        [string]$Text
    )

    $parent = Split-Path -Parent $Path
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    $normalized = ($Text.Trim() -replace "`r?`n", "`r`n")
    Set-Content -LiteralPath $Path -Value $normalized -Encoding ASCII
}

function New-CustomerReadme {
    param([string]$BuildDate)

    return @"
XVatsim
Version: $Version
Build date: $BuildDate
Platform: Windows / X-Plane 12 / xPilot

What XVatsim is:
XVatsim is an xPilot companion plugin for X-Plane 12 that gives the pilot a clean,
route-aware VATSIM frequency display. Instead of showing every nearby controller and
forcing the pilot to sort through clutter, XVatsim focuses on the controllers that are
relevant to the current IFR flight and stage of operation.

Current supported scope:
- Windows only
- X-Plane 12 only
- xPilot required
- IFR flight-plan workflow supported

Install:
1. Close X-Plane.
2. Open your X-Plane 12 root folder.
3. Copy the included Resources folder into the X-Plane 12 root folder.
4. Confirm the final plugin path is:
   X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl
5. Start X-Plane 12.
6. Start xPilot and connect to VATSIM.

Expected behavior:
- XVatsim stays hidden until xPilot connects.
- The overlay wakes with a roll-down animation.
- Departure shows departure-airport local controllers and CTAF or UNICOM fallback.
- Enroute shows route-relevant Center controllers only.
- Arrival wakes ahead of destination and shows destination-relevant approach, tower,
  ground, ATIS, CTAF, and route Center status.
- If xPilot disconnects during a long flight, XVatsim can recover the current flight
  after reconnect when the matching VATSIM flight plan is available again.
- If no route controller is online, the UI can sleep while continuing to monitor.
- Optional Standby Assist can preload COM1 standby with the recommended live controller frequency.
- TX, RX, COM1, COM2, and MODE C status are displayed in the overlay.
- Private messages, PDC/AUTO_ATC cards, SimBrief import, Navigraph AIRAC import,
  dedicated VFR workflow, Mac, Linux, and X-Plane 11 support are not part of this V1 release.

Useful menu items:
- XVatsim > Open Display
- XVatsim > Close Display
- XVatsim > Auto Display
- XVatsim > Standby Assist On / Off
- XVatsim > Set Cruise Target To Current Altitude
- XVatsim > Reset Cruise Target To Filed Altitude
- XVatsim > Reset To Current Flight
- XVatsim > Reset XVatsim Session

If something looks wrong:
- Include the departure and arrival airports
- Include the callsign
- Include what xPilot showed
- Include what XVatsim showed
- Include screenshots if possible
- Include X-Plane Log.txt if the issue can be repeated

Package contents:
- Resources\plugins\XVatsim\win_x64\XVatsim.xpl
- Resources\plugins\XVatsim\win_x64\ui_transition.mp3
- Resources\plugins\XVatsim\win_x64\authority_source_registry.json
- README.txt
- CHANGELOG.txt
- QUICK_START.txt
"@
}

function New-CustomerChangelog {
    param([string]$BuildDate)

    return @"
XVatsim
Version: $Version
Build date: $BuildDate

Release highlights:
- Clean in-cockpit overlay designed to reduce VATSIM frequency clutter
- Brain-owned runtime path for route, workflow, controller relevance, and display intent
- 5 of 5 live battle tests completed for the current release-gate runtime hash
- Reset To Current Flight recovery validated during long-haul reconnect testing
- Departure view shows departure-airport local controllers plus CTAF or UNICOM fallback
- Enroute view follows the filed VATSIM route and monitors route-relevant Center controllers
- Arrival wakes near destination and shows destination-relevant frequencies
- COM1 and COM2 frequency display in the overlay
- TX and RX status boxes mirror radio transmit and receive state
- MODE C active status displayed in the overlay
- Optional Standby Assist can preload COM1 standby with the recommended live controller
- Overlay position, scale, mode, and shell state persist across simulator restarts
- Guard frequency 121.500 and invalid login frequency 199.998 are filtered
- Airway-aware route parsing and route-sector resolution
- Oceanic coordinate-token parsing and date-line handling for long-haul routes
- CTAF fallback resolves to NO CTAF / UNICOM 122.800 when no CTAF is published
- Packaged authority source registry included for source fallback and route authority data
- Experimental private-message/PDC overlay UI is disabled for V1 release stability

Current scope:
- Windows only
- X-Plane 12 only
- xPilot required
- IFR workflow supported

Not included in V1:
- Private-message, PDC, or AUTO_ATC card presentation
- SimBrief import
- Navigraph AIRAC import
- Dedicated VFR workflow
- Mac, Linux, or X-Plane 11 support
"@
}

function New-CustomerQuickStart {
    param([string]$BuildDate)

    return @"
XVatsim Quick Start
Version: $Version
Updated: $BuildDate

Install:
1. Close X-Plane 12.
2. Copy the included Resources folder into your X-Plane 12 root folder.
3. Confirm this file exists after copying:
   X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl
4. Start X-Plane 12.
5. Start xPilot and connect to VATSIM.

Basic operation:
- XVatsim stays hidden until xPilot is connected.
- Auto Display lets XVatsim wake and sleep based on flight context.
- Open Display forces the overlay open.
- Close Display forces the overlay to sleep.
- Reset To Current Flight asks XVatsim to recover the active VATSIM flight plan after reconnect.
- Reset XVatsim Session clears flight-scoped state.

Standby Assist:
- Standby Assist can preload COM1 standby with the recommended live controller frequency.
- The overlay shows ASST ON or ASST OFF so the current assist state is visible.
- CTAF/UNICOM and private-message/PDC handling are not part of Standby Assist in V1.

Supported V1 scope:
- Windows
- X-Plane 12
- xPilot
- IFR flight-plan workflow

Not included in V1:
- Private-message, PDC, or AUTO_ATC card presentation
- SimBrief import
- Navigraph AIRAC import
- Dedicated VFR workflow
- Mac, Linux, or X-Plane 11 support

If something looks wrong:
- Note the departure and arrival airports.
- Note the callsign.
- Note what xPilot showed.
- Note what XVatsim showed.
- Include screenshots and X-Plane Log.txt if the issue can be repeated.
"@
}

function New-KitOverview {
    param(
        [string]$BuildDate,
        [string]$ZipName
    )

    return @"
XVatsim X-Plane.org Store Submission Kit
Prepared: $BuildDate
Public launch version: $Version

Purpose:
This folder is the working submission kit for putting XVatsim on the X-Plane.org Store.
It is split into two parts:

1. XVatsim_${Version}_Windows_XP12
   Customer-facing release package with the plugin, install guide, and changelog.

2. Store_Submission_Materials
   Store-facing materials including product copy, technical specs, pricing notes,
   screenshot shot list, support/update policy, and a vendor contact draft.

What is ready in this kit:
- Current Windows/X-Plane 12 plugin payload refreshed from build\dist
- Transition audio file
- Packaged authority source registry
- Quick-start text guide
- Customer README
- Customer changelog
- Store-upload package zip:
  $ZipName
- Store listing copy
- Store submission checklist
- Support/update policy draft
- Pricing/positioning draft

What still needs to be finalized before submission:
- Final storefront/vendor display name
- Final support email address
- Final screenshots

Recommended next steps:
1. Capture the screenshots listed in Store_Submission_Materials\08_Screenshot_Shot_List.txt.
2. Fill in the support email and vendor/storefront name placeholders.
3. Run tools\release_gate\Run-FinalReleaseValidation.ps1 against this kit.
4. Optionally run one final in-sim smoke test from the final zipped customer package.
5. Submit the validated store-upload zip and store-facing materials.
"@
}

function New-StoreMaterial {
    param([string]$Name)

    switch ($Name) {
        "01" {
            return @"
Suggested product title:
XVatsim - Route-Aware VATSIM Overlay for X-Plane 12

Suggested short tagline:
Clean, route-aware VATSIM frequency awareness for xPilot in X-Plane 12.

Suggested one-line short description:
XVatsim is a companion overlay for xPilot that removes controller clutter and shows only
the VATSIM frequencies relevant to your current IFR flight and stage of operation.

Alternate short description options:
1. A cleaner, smarter VATSIM overlay for xPilot and X-Plane 12 IFR flying.
2. Route-aware VATSIM frequency display built to reduce cockpit clutter in X-Plane 12.
3. XVatsim keeps VATSIM frequency awareness clean, relevant, and easy to trust.
"@
        }
        "02" {
            return @"
XVatsim is a companion plugin for xPilot in X-Plane 12 built around one simple idea:
the pilot should not have to sort through a cluttered controller list while flying on
VATSIM. Instead of showing everything nearby, XVatsim focuses on the frequencies that
actually matter to the current IFR flight and current stage of operation.

The overlay is designed to feel like a real piece of cockpit equipment rather than a flat
utility window. It wakes and sleeps automatically, stays out of the way when it has
nothing useful to show, and comes alive when the pilot needs relevant information.

Core features:
- Brain-owned controller relevance and display intent for current-flight frequency decisions
- Departure display for airport local services such as clearance, ground, tower, approach/departure, and CTAF
- Enroute display that follows the filed VATSIM route and monitors route-relevant Center controllers
- Arrival display for destination-relevant Center, approach, tower, ground, ATIS, and CTAF/UNICOM fallback
- Reset To Current Flight recovery for reconnecting to the active VATSIM plan after long-haul disconnects
- Optional COM1 Standby Assist to preload the recommended live controller frequency
- COM1 and COM2 frequency readout directly in the overlay
- TX and RX status boxes that mirror radio transmit and receive state
- MODE C status indication in the overlay
- Compact ASST ON / ASST OFF status so pilots can see whether Standby Assist is enabled
- Persistent overlay size, position, display mode, and shell state across simulator restarts
- Airway-aware route parsing, controller authority catalog support, duplicate-waypoint protection,
  date-line handling, and long-haul route improvements

Why XVatsim exists:
xPilot already does the hard work of being the VATSIM client. XVatsim is not trying to
replace it. XVatsim exists to make controller awareness calmer, cleaner, and more
intuitive in the cockpit by surfacing only the information the pilot is likely to need.

Important scope:
- Windows only
- X-Plane 12 only
- xPilot required
- Built and validated around IFR flight-plan operations

Not included in this V1 release:
- Mac and Linux are not part of this release
- X-Plane 11 is not part of this release
- VFR-specific workflow is planned separately and is not the focus of this version
- Private-message, PDC, and AUTO_ATC card presentation are not part of this release
- SimBrief import and Navigraph AIRAC import are not part of this release

Recommended category:
Utilities > Traffic / ATC
"@
        }
        "03" {
            return @"
Product:
XVatsim

Recommended store title:
XVatsim - Route-Aware VATSIM Overlay for X-Plane 12

Public version:
$Version

Platform support:
- Windows
- X-Plane 12

Required dependency:
- xPilot

Supported workflow:
- IFR flight-plan operations

Install path:
X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl

Included plugin payload:
- XVatsim.xpl
- ui_transition.mp3
- authority_source_registry.json

Operational highlights:
- Hidden until xPilot connects
- Auto, manual-open, and manual-close display behavior
- Roll-down / roll-up animated shell
- Brain-owned route, workflow, relevance, and display decisions
- Reset To Current Flight reconnect recovery
- CTAF fallback to NO CTAF / UNICOM 122.800 when no CTAF is published
- COM1 standby assist option for recommended live controller frequencies
- TX / RX / MODE C visual status
- ASST ON / ASST OFF visual status
- Airway-aware route parsing and route-sector resolution
- Controller authority catalog support

Not included in this release:
- Mac support
- Linux support
- X-Plane 11 support
- Dedicated VFR workflow
- Private-message, PDC, or AUTO_ATC card presentation
- SimBrief import
- Navigraph AIRAC import

Suggested store tags/keywords:
- VATSIM
- xPilot
- ATC
- overlay
- IFR
- utility
- cockpit
- communications
"@
        }
        "04" {
            return @"
Recommended launch price:
`$7.99 USD

Positioning:
XVatsim should be priced as an affordable, high-utility cockpit enhancement rather than
as a premium aircraft-level add-on. It improves usability and pilot awareness without
trying to replace xPilot itself.

Why `$7.99 makes sense:
- Low-friction impulse-buy range for VATSIM users
- Easy value proposition: cleaner controller awareness, less cockpit clutter
- Fair price for a focused utility rather than a full ecosystem product
- Encourages community adoption and word of mouth

Optional launch strategy:
- Standard launch: `$7.99
- Optional intro sale: `$6.99 for a short launch window

Recommended positioning sentence:
XVatsim is a small but high-value cockpit utility built to make VATSIM flying cleaner,
faster, and easier to trust.
"@
        }
        "05" {
            return @"
Suggested support policy:

Support channel:
- Replace with your preferred support email or support forum thread
- Suggested placeholder: [replace-with-support-email]

Bug-report expectations:
Ask customers to include:
- Departure airport
- Arrival airport
- Callsign
- What xPilot showed
- What XVatsim showed
- Screenshots if possible
- X-Plane Log.txt if the issue can be repeated

Suggested update policy:
- Free updates within the same major version
- Critical bug-fix updates prioritized
- Feature updates delivered as time and roadmap allow

Suggested public support note:
XVatsim is actively supported, but support works best when users include screenshots and
their X-Plane Log.txt so behavior can be verified accurately.

Suggested expectations note:
This release is focused on Windows, X-Plane 12, xPilot, and IFR flight-plan operations.
"@
        }
        "06" {
            return @"
XVatsim X-Plane.org Store Submission Checklist

Store contact:
- X-Plane.org contact page: https://forums.x-plane.org/contactus/
- If you have a product you want to add to the store, their contact page says to email:
  [email protected]

Before contacting the store:
- Confirm the plugin works from a clean install using only the packaged files
- Confirm the package path is correct
- Confirm QUICK_START.txt matches the final packaged behavior
- Confirm README and changelog match the final release version
- Decide final product title
- Decide final vendor/storefront name
- Decide final support email

Store assets to prepare:
- Customer package zip
- 5 to 8 strong product screenshots
- Final short description
- Final long description
- Technical specs
- Support/update policy
- Changelog / release notes

Before upload:
- Run tools\release_gate\Run-FinalReleaseValidation.ps1 against this kit
- Confirm no rc/beta/preview wording appears in customer-facing package files
- Name the package zip for the final public version
- Remove any internal-only notes you do not want visible to customers
- Double-check that no test-only files are included

After store approval:
- Keep a copy of the exact uploaded zip
- Keep a copy of the exact listing text
- Create a public support thread or support mailbox if desired
"@
        }
        "07" {
            return @"
Subject: New X-Plane.org Store Product Submission - XVatsim

Hello,

I would like to submit a new X-Plane 12 utility plugin for consideration on the
X-Plane.org Store.

Product name:
XVatsim - Route-Aware VATSIM Overlay for X-Plane 12

Product summary:
XVatsim is a companion plugin for xPilot that provides a clean, route-aware VATSIM
frequency overlay in X-Plane 12. It is designed to reduce controller-list clutter and
show the pilot only the frequencies relevant to the current IFR flight and stage of
operation.

Current supported scope:
- Windows only
- X-Plane 12 only
- xPilot required
- IFR workflow focused

Planned launch price:
`$7.99 USD

Included with this submission:
- Customer-ready package zip
- Install/readme file
- Changelog
- Quick-start text guide
- Product description and technical details
- Screenshots

Please let me know the preferred next step for store submission, review, and any
additional requirements you would like included.

Best regards,
[replace-with-your-name]
[replace-with-vendor-or-brand-name]
[replace-with-support-email]
"@
        }
        "08" {
            return @"
Recommended screenshot list for the store page

Goal:
Show that XVatsim is polished, minimal, and useful under real IFR VATSIM conditions.

Priority screenshots:
1. Busy departure airport
   Show departure controllers displayed cleanly and in order.
   Suggested file name: 01_departure_busy_airport.png

2. Enroute with live Center controller
   Show the route-aware enroute view with only relevant online Centers.
   Suggested file name: 02_enroute_live_center.png

3. Arrival with approach plus Center plus CTAF/UNICOM
   Show the arrival board with destination-relevant frequencies only.
   Suggested file name: 03_arrival_destination_services.png

4. European no-CTAF fallback
   Show NO CTAF / UNICOM 122.800 at a suitable airport.
   Suggested file name: 04_europe_unicom_fallback.png

5. COM1 standby assist / radio status
   Show COM1, COM2, TX, RX, MODE C, and ASST ON / ASST OFF in a clean cockpit view.
   Suggested file name: 05_radio_status_and_standby_assist.png

6. Reset To Current Flight recovery
   Show the menu item or recovered current-flight display after reconnect.
   Suggested file name: 06_reset_to_current_flight.png

Optional screenshots:
7. Pacific or transatlantic route
   Suggested file name: 07_longhaul_route_validation.png

8. Settings/menu control shot
   Suggested file name: 08_menu_and_controls.png

Screenshot guidance:
- Use bright, readable cockpit lighting
- Keep the overlay unobstructed
- Avoid overexposed outside scenery if possible
- Prefer sharp examples where the controller logic is clearly visible
- Capture a mix of departure, enroute, and arrival situations
"@
        }
        "09" {
            return @"
Still needed before final store submission:

Required:
- Final screenshots
- Final storefront/vendor name
- Final support email

Recommended:
- One strong hero image for the product page
- One clean product icon/logo if you want branding beyond screenshots
- A short public support note or support thread link

Notes:
- This kit intentionally avoids inventing store-image dimension requirements.
- If the store provides specific artwork size requirements after contact, match those exactly.
"@
        }
    }
}

function New-ImagesReadme {
    return @"
Drop your final store screenshots into this folder.

Suggested file names:
- 01_departure_busy_airport.png
- 02_enroute_live_center.png
- 03_arrival_destination_services.png
- 04_europe_unicom_fallback.png
- 05_radio_status_and_standby_assist.png
- 06_reset_to_current_flight.png
- 07_longhaul_route_validation.png
- 08_menu_and_controls.png

You do not need all eight.
If you only use five or six, prioritize the first six names in the list.
"@
}

$root = Resolve-WorkspaceRoot
$releaseRoot = Join-Path $root "releases"
$buildDir = Join-Path $root "build"
$distDir = Join-Path $buildDir "dist\XVatsim\win_x64"
$kitDateForName = $KitDate -replace "-", "-"
$kitRoot = Join-Path $releaseRoot "XVatsim_XPlaneOrg_Store_Submission_Kit_${Version}_$kitDateForName"
$customerPackageName = "XVatsim_${Version}_Windows_XP12"
$customerPackageRoot = Join-Path $kitRoot $customerPackageName
$pluginPayloadRoot = Join-Path $customerPackageRoot "Resources\plugins\XVatsim\win_x64"
$storeMaterialsRoot = Join-Path $kitRoot "Store_Submission_Materials"
$zipName = "${customerPackageName}_store_upload_$($KitDate -replace '-', '').zip"
$zipPath = Join-Path $kitRoot $zipName

Write-Host "XVatsim store submission package builder"
Write-Host "Workspace: $root"
Write-Host "Kit: $kitRoot"

if (-not $SkipBuild) {
    Write-Step "Building release plugin"
    $cmake = Get-CMakePath
    Invoke-Checked $cmake @("--build", $buildDir, "--config", "Release", "--target", "XVatsimPlugin") "Release plugin build"
} else {
    Write-Step "Skipping build by request"
}

Write-Step "Preparing clean kit folder"
New-Item -ItemType Directory -Path $releaseRoot -Force | Out-Null
Assert-UnderRoot -Root $releaseRoot -Path $kitRoot -Label "Store submission kit"
if (Test-Path -LiteralPath $kitRoot) {
    Remove-Item -LiteralPath $kitRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $pluginPayloadRoot -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $storeMaterialsRoot "Store_Images_To_Add") -Force | Out-Null

$artifacts = @(
    @{
        Label = "XVatsim.xpl"
        Source = Join-Path $distDir "XVatsim.xpl"
        Relative = "Resources\plugins\XVatsim\win_x64\XVatsim.xpl"
    },
    @{
        Label = "ui_transition.mp3"
        Source = Join-Path $distDir "ui_transition.mp3"
        Relative = "Resources\plugins\XVatsim\win_x64\ui_transition.mp3"
    },
    @{
        Label = "authority_source_registry.json"
        Source = Join-Path $distDir "authority_source_registry.json"
        Relative = "Resources\plugins\XVatsim\win_x64\authority_source_registry.json"
    }
)

Write-Step "Copying release payload"
foreach ($artifact in $artifacts) {
    Assert-File $artifact.Source "$($artifact.Label) build artifact"
    $destination = Join-Path $customerPackageRoot $artifact.Relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $artifact.Source -Destination $destination -Force
    Write-Host "$($artifact.Label): $(Get-Sha256 $destination)"
}

Write-Step "Writing customer documents"
Write-TextFile -Path (Join-Path $customerPackageRoot "README.txt") -Text (New-CustomerReadme -BuildDate $KitDate)
Write-TextFile -Path (Join-Path $customerPackageRoot "CHANGELOG.txt") -Text (New-CustomerChangelog -BuildDate $KitDate)
Write-TextFile -Path (Join-Path $customerPackageRoot "QUICK_START.txt") -Text (New-CustomerQuickStart -BuildDate $KitDate)

Write-Step "Writing store submission materials"
Write-TextFile -Path (Join-Path $storeMaterialsRoot "01_Product_Tagline_and_Short_Description.txt") -Text (New-StoreMaterial -Name "01")
Write-TextFile -Path (Join-Path $storeMaterialsRoot "02_Long_Store_Description.txt") -Text (New-StoreMaterial -Name "02")
Write-TextFile -Path (Join-Path $storeMaterialsRoot "03_Technical_Specifications.txt") -Text (New-StoreMaterial -Name "03")
Write-TextFile -Path (Join-Path $storeMaterialsRoot "04_Pricing_and_Positioning.txt") -Text (New-StoreMaterial -Name "04")
Write-TextFile -Path (Join-Path $storeMaterialsRoot "05_Support_and_Update_Policy.txt") -Text (New-StoreMaterial -Name "05")
Write-TextFile -Path (Join-Path $storeMaterialsRoot "06_XPlaneOrg_Submission_Checklist.txt") -Text (New-StoreMaterial -Name "06")
Write-TextFile -Path (Join-Path $storeMaterialsRoot "07_Vendor_Contact_Email_Draft.txt") -Text (New-StoreMaterial -Name "07")
Write-TextFile -Path (Join-Path $storeMaterialsRoot "08_Screenshot_Shot_List.txt") -Text (New-StoreMaterial -Name "08")
Write-TextFile -Path (Join-Path $storeMaterialsRoot "09_Assets_Still_Needed.txt") -Text (New-StoreMaterial -Name "09")
Write-TextFile -Path (Join-Path $storeMaterialsRoot "Store_Images_To_Add\README.txt") -Text (New-ImagesReadme)

Write-Step "Creating store-upload zip"
Compress-Archive -LiteralPath $customerPackageRoot -DestinationPath $zipPath -Force
$zipHash = Get-Sha256 $zipPath
$zipSize = (Get-Item -LiteralPath $zipPath).Length

$scenarioDir = Join-Path $root "tools\regression_harness\scenarios"
$scenarioCount = @(Get-ChildItem -LiteralPath $scenarioDir -Filter "*.scn" -File -ErrorAction SilentlyContinue).Count
$xplPath = Join-Path $pluginPayloadRoot "XVatsim.xpl"
$audioPath = Join-Path $pluginPayloadRoot "ui_transition.mp3"
$registryPath = Join-Path $pluginPayloadRoot "authority_source_registry.json"

$audit = @"
XVatsim V1 Release Audit
Updated: $KitDate

Current customer package payload:
- Resources\plugins\XVatsim\win_x64\XVatsim.xpl
  SHA256: $(Get-Sha256 $xplPath)
- Resources\plugins\XVatsim\win_x64\ui_transition.mp3
  SHA256: $(Get-Sha256 $audioPath)
- Resources\plugins\XVatsim\win_x64\authority_source_registry.json
  SHA256: $(Get-Sha256 $registryPath)
- README.txt
- CHANGELOG.txt
- QUICK_START.txt

Final store-upload package:
- $zipName
- Size: $zipSize bytes
- SHA256: $zipHash
- Built after the 5 of 5 live battle-test gate completed for the current runtime hash.
- Must be validated by tools\release_gate\Run-FinalReleaseValidation.ps1 before upload.

Verified release behavior documented for V1:
- Windows / X-Plane 12 / xPilot / IFR flight-plan workflow
- Automatic wake/sleep overlay behavior after xPilot connection
- Brain-owned route, workflow, controller relevance, and display intent
- Route-aware enroute Center selection
- Current-polygon and next-polygon display relation coloring
- CTAF/UNICOM display fallback on airport boards
- Optional Standby Assist for recommended live controller frequencies
- COM1, COM2, TX, RX, MODE C, and ASST ON/OFF display
- Reset To Current Flight reconnect recovery
- Airway-aware route parsing, authority-catalog matching, date-line handling, and duplicate-waypoint protection

Explicitly not included in V1:
- Private-message, PDC, or AUTO_ATC card presentation
- SimBrief import or comparison
- Navigraph AIRAC import or local navdata graph
- Dedicated VFR workflow
- Mac, Linux, or X-Plane 11 support

Release-seam decisions:
- Experimental controller/private-message UI is gated off by default.
- The packaged authority source registry is included next to the plugin binary.
- The packaged binary must be refreshed from build\dist\XVatsim\win_x64 before zip creation.
- The final package should contain no Debug, Release, RelWithDebInfo, tmp_*, desktop.ini, .pdb, .lib, .exp, .tmp, .log, or .pdf files.
- Saved regression scenarios available for final gate replay: $scenarioCount.

Still required before upload:
- Final screenshots
- Final storefront/vendor display name
- Final support email address
- Final release validation gate pass
- Optional final in-sim smoke test from the final zipped customer package
"@

Write-TextFile -Path (Join-Path $storeMaterialsRoot "10_V1_Release_Audit.txt") -Text $audit
Write-TextFile -Path (Join-Path $kitRoot "KIT_OVERVIEW.txt") -Text (New-KitOverview -BuildDate $KitDate -ZipName $zipName)

Write-Step "Store submission package created"
Write-Host "Kit root: $kitRoot"
Write-Host "Customer package: $customerPackageRoot"
Write-Host "Store zip: $zipPath"
Write-Host "Store zip SHA-256: $zipHash"
