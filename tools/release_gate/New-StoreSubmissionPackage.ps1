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

function Get-LicenseText {
    param(
        [string]$Root,
        [string]$BuildDate
    )

    $templatePath = Join-Path $Root "docs\legal\XVatsim_EULA_TEMPLATE.txt"
    Assert-File $templatePath "XVatsim EULA template"

    $text = Get-Content -LiteralPath $templatePath -Raw
    return $text.Replace("{{VERSION}}", $Version).Replace("{{BUILD_DATE}}", $BuildDate)
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
- Standby Assist can preload COM1 standby with the selected live controller frequency.
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
- Send support requests to ezsimulations@gmail.com
- Support may require proof of purchase from the X-Plane.org Store

Package contents:
- Resources\plugins\XVatsim\win_x64\XVatsim.xpl
- Resources\plugins\XVatsim\win_x64\ui_transition.mp3
- Resources\plugins\XVatsim\win_x64\authority_source_registry.json
- README.txt
- CHANGELOG.txt
- QUICK_START.txt
- LICENSE.txt
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
- Standby Assist can preload COM1 standby with the selected live controller
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
- Standby Assist can preload COM1 standby with the selected live controller frequency.
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
- Send support requests to ezsimulations@gmail.com.
- Support may require proof of purchase from the X-Plane.org Store.

License:
- XVatsim is commercial software from EZ SIMULATIONS.
- Read LICENSE.txt before installing or using XVatsim.
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
- Customer license/EULA
- Store-upload package zip:
  $ZipName
- Store listing copy
- Store submission checklist
- Support/update policy draft
- Pricing/positioning draft
- Anti-piracy and proof-of-purchase policy draft

What still needs to be finalized before submission:
- Confirm whether the X-Plane.org Store can issue/display serial or license keys for XVatsim
- Store screenshots are already placed in Store_Submission_Materials\Store_Images_To_Add

Submission steps:
1. Review LICENSE.txt and Store_Submission_Materials\12_License_and_Anti_Piracy_Policy.txt.
2. Confirm whether the store can attach serial/license keys to the product listing.
3. Run tools\release_gate\Run-FinalReleaseValidation.ps1 against this kit.
4. Run one final in-sim smoke test from the final zipped customer package when time allows.
5. Submit the validated store-upload zip and store-facing materials.
"@
}

function New-StoreMaterial {
    param([string]$Name)

    switch ($Name) {
        "01" {
            return @"
Product title:
XVatsim - Route-Aware VATSIM Overlay for X-Plane 12

Short tagline:
Clean, route-aware VATSIM frequency awareness for xPilot in X-Plane 12.

One-line short description:
XVatsim is a companion overlay for xPilot that removes controller clutter and shows only
the VATSIM frequencies relevant to your current IFR flight and stage of operation.
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
- COM1 Standby Assist can preload the next live controller frequency
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

Store category:
Utilities > Traffic / ATC
"@
        }
        "03" {
            return @"
Product:
XVatsim

Store title:
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
- COM1 standby assist for the next live controller frequency
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

Store tags/keywords:
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
Launch price:
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

Launch plan:
- Standard launch price: `$7.99
- Introductory sale is not part of the first submission packet.

Positioning sentence:
XVatsim is a small but high-value cockpit utility built to make VATSIM flying cleaner,
faster, and easier to trust.
"@
        }
        "05" {
            return @"
Support policy:

Support channel:
- ezsimulations@gmail.com

Bug-report expectations:
Ask customers to include:
- Departure airport
- Arrival airport
- Callsign
- What xPilot showed
- What XVatsim showed
- Screenshots if possible
- X-Plane Log.txt if the issue can be repeated

Update policy:
- Free updates within the same major version
- Critical bug-fix updates prioritized
- Feature updates delivered as time and roadmap allow

Public support note:
XVatsim is actively supported, but support works best when users include screenshots and
their X-Plane Log.txt so behavior can be verified accurately.

Release scope note:
This release is focused on Windows, X-Plane 12, xPilot, and IFR flight-plan operations.

Proof-of-purchase policy:
Support, replacement downloads, update help, and licensing help may require proof of
purchase through the X-Plane.org Store.
"@
        }
        "06" {
            return @"
XVatsim X-Plane.org Store Submission Checklist

Store contact:
- X-Plane.org contact page: https://forums.x-plane.org/contactus/
- Use the email address shown under "Add my product to the store" on that page.

Before contacting the store:
- Confirm the plugin works from a clean install using only the packaged files
- Confirm the package path is correct
- Confirm QUICK_START.txt matches the final packaged behavior
- Confirm README and changelog match the final release version
- Product title is locked: XVatsim - Route-Aware VATSIM Overlay for X-Plane 12
- Confirm vendor/storefront name: EZ SIMULATIONS
- Confirm support email: ezsimulations@gmail.com
- Review LICENSE.txt
- Ask whether the X-Plane.org Store can issue/display serial or license keys for XVatsim

Store assets included:
- Customer package zip
- 5 product screenshots
- Short description
- Long description
- Technical specs
- Support/update policy
- License and anti-piracy policy
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
- Keep support routed through ezsimulations@gmail.com unless the store requests a forum thread
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

Before launch, could you also confirm whether the X-Plane.org Store can issue or
display serial/license keys for XVatsim? The current V1 package includes a proprietary
EULA and proof-of-purchase support policy. I would like to understand the store's
preferred option for license-key handling before adding any runtime activation
system.

Best regards,
Darron
EZ SIMULATIONS
ezsimulations@gmail.com
"@
        }
        "08" {
            return @"
Store screenshot inventory

Goal:
Show that XVatsim is polished, minimal, and useful under real IFR VATSIM conditions.

Screenshots included in Store_Images_To_Add:
1. 01_clean_ui.jpg
   Clean overlay state with the cockpit visible.

2. 02_closed_shell.jpg
   Closed shell / sleep-state view showing how the overlay stays out of the way.

3. 03_europe_unicom.jpg
   European no-CTAF / UNICOM fallback behavior.

4. 04_airport_authority.jpg
   Airport authority display with current-flight relevance.

5. 05_center_frequency_display.jpg
   Center frequency display for route-aware enroute or arrival context.

Screenshot status:
- Store-facing JPG screenshots are placed in Store_Submission_Materials\Store_Images_To_Add.
- Source screenshots came from the Store Ready screenshot folder and were converted to normal JPG for submission.
- The current image set covers clean UI, closed shell, European UNICOM fallback, airport authority, and Center display.
"@
        }
        "09" {
            return @"
Final submission attachment checklist

Attach to the store email:
- XVatsim_1.0.0_Windows_XP12_store_upload_20260527.zip
- Store_Submission_Materials\01_Product_Tagline_and_Short_Description.txt
- Store_Submission_Materials\02_Long_Store_Description.txt
- Store_Submission_Materials\03_Technical_Specifications.txt
- Store_Submission_Materials\04_Pricing_and_Positioning.txt
- Store_Submission_Materials\05_Support_and_Update_Policy.txt
- Store_Submission_Materials\06_XPlaneOrg_Submission_Checklist.txt
- Store_Submission_Materials\07_Vendor_Contact_Email_Draft.txt
- Store_Submission_Materials\08_Screenshot_Shot_List.txt
- Store_Submission_Materials\10_V1_Release_Audit.txt
- Store_Submission_Materials\11_Final_Validation_Result.txt
- Store_Submission_Materials\12_License_and_Anti_Piracy_Policy.txt
- Store_Submission_Materials\Store_Images_To_Add\01_clean_ui.jpg
- Store_Submission_Materials\Store_Images_To_Add\02_closed_shell.jpg
- Store_Submission_Materials\Store_Images_To_Add\03_europe_unicom.jpg
- Store_Submission_Materials\Store_Images_To_Add\04_airport_authority.jpg
- Store_Submission_Materials\Store_Images_To_Add\05_center_frequency_display.jpg

Store-side question to include:
- Can the X-Plane.org Store issue or display serial/license keys for XVatsim?

Notes:
- The store-upload zip is the customer package.
- The Store_Submission_Materials folder is for the store review email and listing setup.
- If the store requests different image dimensions or file naming, resize/copy the JPG screenshots without changing the validated customer zip.
"@
        }
        "12" {
            return @"
XVatsim License and Anti-Piracy Policy

Publisher:
EZ SIMULATIONS

Support:
ezsimulations@gmail.com

Customer package license:
- LICENSE.txt is included in the root of the customer package.
- The license grants personal simulator use only.
- The customer does not receive ownership of XVatsim, source code, data files, artwork,
  audio, product identity, or other intellectual property.
- Redistribution, resale, upload, file sharing, account sharing, license sharing, and
  presenting XVatsim as freeware are prohibited.
- Support and update help may require proof of purchase from the X-Plane.org Store.

V1 anti-piracy decision:
- Do not add runtime DRM or online activation in this V1 package.
- The current five-pass runtime hash should remain untouched for store submission.
- Adding runtime activation later must go through a separate runtime Contract Gate and
  live validation reset if it changes plugin behavior.

Store licensing question:
Ask whether the X-Plane.org Store can issue/display serial or license keys for XVatsim.
If the store supports this cleanly, use the store-managed serial as the commercial
purchase credential first. Runtime enforcement can remain a later, deliberate V2
commercial-hardening project.

Practical piracy response:
- Keep the exact uploaded zip and hash on file.
- Require proof of purchase for support, replacement downloads, and update help.
- If XVatsim appears on unauthorized sites, use the EULA, store listing, uploaded zip
  hash, and ownership records as takedown evidence.
- Treat heavy runtime DRM as a separate product decision, not as a late packaging patch.
"@
        }
    }
}

function New-ImagesReadme {
    return @"
Store screenshots for the XVatsim submission.

Expected JPG files:
- 01_clean_ui.jpg
- 02_closed_shell.jpg
- 03_europe_unicom.jpg
- 04_airport_authority.jpg
- 05_center_frequency_display.jpg

These files are store-facing assets. They are not included in the customer upload zip.
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
Write-TextFile -Path (Join-Path $customerPackageRoot "LICENSE.txt") -Text (Get-LicenseText -Root $root -BuildDate $KitDate)

Write-Step "Writing store submission materials"
Write-TextFile -Path (Join-Path $storeMaterialsRoot "01_Product_Tagline_and_Short_Description.txt") -Text (New-StoreMaterial -Name "01")
Write-TextFile -Path (Join-Path $storeMaterialsRoot "02_Long_Store_Description.txt") -Text (New-StoreMaterial -Name "02")
Write-TextFile -Path (Join-Path $storeMaterialsRoot "03_Technical_Specifications.txt") -Text (New-StoreMaterial -Name "03")
Write-TextFile -Path (Join-Path $storeMaterialsRoot "04_Pricing_and_Positioning.txt") -Text (New-StoreMaterial -Name "04")
Write-TextFile -Path (Join-Path $storeMaterialsRoot "05_Support_and_Update_Policy.txt") -Text (New-StoreMaterial -Name "05")
Write-TextFile -Path (Join-Path $storeMaterialsRoot "06_XPlaneOrg_Submission_Checklist.txt") -Text (New-StoreMaterial -Name "06")
Write-TextFile -Path (Join-Path $storeMaterialsRoot "07_Vendor_Contact_Email_Draft.txt") -Text (New-StoreMaterial -Name "07")
Write-TextFile -Path (Join-Path $storeMaterialsRoot "08_Screenshot_Shot_List.txt") -Text (New-StoreMaterial -Name "08")
Write-TextFile -Path (Join-Path $storeMaterialsRoot "09_Final_Attachment_Checklist.txt") -Text (New-StoreMaterial -Name "09")
Write-TextFile -Path (Join-Path $storeMaterialsRoot "12_License_and_Anti_Piracy_Policy.txt") -Text (New-StoreMaterial -Name "12")
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
- Standby Assist for selected live controller frequencies
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
- LICENSE.txt is included in the customer package and support may require proof of purchase.
- No runtime DRM or online activation was added to this V1 store package.
- The packaged binary must be refreshed from build\dist\XVatsim\win_x64 before zip creation.
- The final package should contain no Debug, Release, RelWithDebInfo, tmp_*, desktop.ini, .pdb, .lib, .exp, .tmp, .log, or .pdf files.
- Saved regression scenarios available for final gate replay: $scenarioCount.

Store-side follow-up before upload:
- Confirm whether the X-Plane.org Store can issue/display serial or license keys for XVatsim
- Final release validation gate pass
- Final in-sim smoke test from the zipped customer package when time allows
"@

Write-TextFile -Path (Join-Path $storeMaterialsRoot "10_V1_Release_Audit.txt") -Text $audit
Write-TextFile -Path (Join-Path $kitRoot "KIT_OVERVIEW.txt") -Text (New-KitOverview -BuildDate $KitDate -ZipName $zipName)

Write-Step "Store submission package created"
Write-Host "Kit root: $kitRoot"
Write-Host "Customer package: $customerPackageRoot"
Write-Host "Store zip: $zipPath"
Write-Host "Store zip SHA-256: $zipHash"
