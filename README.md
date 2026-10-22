# XVatsim

XVatsim is a Windows/X-Plane 12 companion plugin for xPilot. It provides a
route-aware cockpit overlay for VATSIM controller awareness, focused on IFR
flight-plan operations.

## Current Release

XVatsim V2.2.0 is the current public freeware release for Windows, X-Plane 12,
xPilot 3, and xPilot 4.

- Freeware package:
  [XVatsim_2.2.0_Freeware_Windows_XP12.zip](https://github.com/ezsimulations/XVatsim/releases/download/v2.2.0/XVatsim_2.2.0_Freeware_Windows_XP12.zip)
- Package size: `64532182` bytes
- Package SHA-256:
  `74B09EA9851118889F6617CEE5C6D28E1C3F8AEC8379E68B34E56A396E6B055D`
- Packaged Manager SHA-256:
  `D955B125A3D2015A74393CC5ECF17B2AF1D586D71CC68679914A79736A967A89`
- Packaged plugin SHA-256:
  `ADFF5F898F1930651C90B5108CF145D84162770C0BEF79965341ABD44B1EC23E`
- Manager update payload SHA-256:
  `259D3A3A4F2AC84531FDCD7D851D5FA6606C9F6B9ED1D60B5DB62BF2375C0D1E`
- User guide:
  `docs/user_guide/XVatsim_User_Guide.pdf`
- Download pages:
  [X-Plane.org](https://forums.x-plane.org/files/file/100224-xvatsim_100_freeware_windows_xp12zip/)
  and [GitHub Releases](https://github.com/ezsimulations/XVatsim/releases/tag/v2.2.0)

## Version 2.2.0 Controller, Assist, Electrical, And Manager Changes

- Strengthens the Brain-owned controller vote so several independent hard
  facts can outweigh a weak callsign spelling difference without allowing one
  fact or worker to decide the result.
- Builds route-relevant departure and arrival terminal polygon stacks,
  including specific inner Approach/Departure areas and their larger parent
  sectors, without processing unrelated terminal polygons worldwide.
- Preserves established Australian callsign abbreviations and fail-safe
  display behavior when valid evidence remains tied or incomplete.
- Adds regional Standby Assist ordering for CTAF and non-CTAF arrivals and
  loads UNICOM `122.800` during Enroute when no eligible controller is online.
- Broadens aircraft electrical-state evidence so custom aircraft can keep
  XVatsim active through battery, GPU, APU, and generator power changes.
- Bounds diagnostic-file growth and route-scopes detailed controller evidence
  while retaining the proof needed to diagnose Brain decisions.
- Adds verified online payload retrieval to the standalone Manager. The
  Manager validates HTTPS, size, SHA-256, version, safe inventory, and every
  managed file before offering a user-selected update with rollback.

Pilots upgrading from V2.1.0 must download the complete V2.2.0 package once
and run the new `XVatsimManager.exe` included in it. The V2.2.0 Manager can
install future compatible XVatsim payloads directly after verifying them, so a
new Manager download is only needed if a future release explicitly requires
one.

## Version 2.1.0 Manager And xPilot 4 Changes

- Adds the standalone XVatsim Manager for guided first installation, update,
  repair, SHA-256 verification, transaction backup, and automatic rollback.
- Automatically installs the read-only companion required for complete xPilot
  4 support while leaving xPilot 3 on the proven legacy integration.
- Consumes xPilot 4 SDK observations without independently connecting to the
  VATSIM network. The bridge reports facts; the XVatsim Brain remains the only
  controller and message decision maker.
- Accepts the approved xPilot 4 SDK 0.1.x family and refuses an incompatible
  future SDK before subscribing to events, so ordinary xPilot 4 beta updates
  do not require a new Manager solely because the beta number changed.
- Recovers retained xPilot 4 PDC/ACARS information at startup and wraps long
  message text in the XVatsim Drawer.
- Derives useful flight rules from METAR ceiling and visibility evidence when
  the source classification is stale or unknown, and advances the received-age
  display after download.
- Keeps the Brain-owned Australia coverage, controller-distance, polygon,
  diagnostic, and PDC behavior established in V2.0.2.

The Manager is distributed as unsigned freeware. Windows SmartScreen may show
an unknown-publisher warning. The package includes plain `More info` and
`Run anyway` instructions plus `SHA256SUMS.txt` for file verification.

## Version 2.0.2 Controller And PDC Changes

- Keeps controller selection in the Brain with simple yes, no, and neutral
  evidence votes from VATSIM, VATSpy, airport distance, route geometry,
  declared coverage extensions, and US vNAS data when available.
- Recognizes Melbourne controller callsign variations and declared Australian
  Center extensions without allowing a worker to suppress a candidate.
- Monitors xPilot's network log every five seconds for incoming direct private
  and PDC/ACARS messages while excluding public radio, broadcast, server,
  outgoing, other-session, and other-callsign traffic.
- Stores admitted messages newest first. The PDC ORB shows amber `NEW` for
  unread content and returns to cyan `IDLE` after the pilot leaves the Drawer.
- Replaces routine per-callback diagnostic records with one-minute timing
  summaries while retaining rate-limited outliers and change-driven Brain
  decision receipts.
- Preserves route-polygon colors, controller distance, Standby Assist, METAR,
  ATIS, CTAF, and the V2.0.1 settled performance path.

## Version 2.0.1 Maintenance Changes

- Adds a Brain-owned settled operational refresh gate. Unchanged simulator
  callbacks take a lightweight path while relevant VATSIM, controller, radio,
  presentation, PDC, air/ground, activation, and worker-state changes wake the
  full refresh immediately.
- Retains a one-second safety refresh instead of allowing settled state to
  remain unobserved indefinitely.
- Avoids repeated inactive vNAS terminal-evidence scans outside the supported
  United States scope.
- Avoids unnecessary diagnostic job formatting and collection between
  scheduled diagnostic frames.
- Preserves the accepted controller-selection, information ORB, Standby Assist,
  and UI behavior.
- Does not adopt the beta X-Plane SDK 4.4 renderer. Panel Graphics is recorded
  as a separate future engineering milestone.

## Version 2.0.0 Feature Foundation

- Adds three Brain-owned information ORBs and drawers: VATSIM METAR, VATSIM
  ATIS, and a one-shot xPilot waiting-message PDC snapshot.
- Strengthens US Approach/Departure selection with vNAS sector evidence while
  keeping the Brain as the sole decision owner.
- Evaluates terminal-controller transmitter distance from the departure or
  arrival airport instead of from the moving aircraft.
- Preserves the aircraft-distance gate for Center presentation within 250
  nautical miles.
- Prevents unchanged ATIS content from repeatedly returning to unread when
  VATSIM republishes the same information.
- Moves route and authority preparation away from the X-Plane flight loop and
  keeps settled operation calm and low-churn.
- Includes IFR/VFR mode selection, diversion handling, current-flight recovery,
  update notifications, CTAF/UNICOM fallback, and exact route-polygon crossing.

For xPilot 3, the PDC feature monitors xPilot's network log at a low five-second
cadence after complete flight identity and a stable connection are available.
For xPilot 4, the companion reports SDK message observations and can recover
retained PDC/ACARS information at startup. In both paths, the Brain admits
incoming direct messages addressed to the active callsign and keeps a bounded
newest-first history. xPilot remains the authoritative VATSIM client.

## Current Scope

- Windows
- X-Plane 12
- xPilot 3 or xPilot 4
- IFR flight-plan controller workflow
- Departure, Enroute, and Arrival controller-board ownership
- Route-aware center selection using typed route parsing and authority catalogs
- US terminal-sector confirmation using vNAS data
- COM1/COM2, TX/RX, Mode C, and Standby Assist status
- VATSIM METAR and ATIS information drawers
- PDC/ACARS and incoming private-message Drawer sourced from the xPilot 3 log
  monitor or the read-only xPilot 4 SDK companion

Not included:

- Mac, Linux, or X-Plane 11 support
- A dedicated VFR controller-evidence workflow
- SimBrief or Navigraph AIRAC import
- Public radio-chat display or AUTO_ATC card
- Second-monitor/out-of-sim window mode

## Reliability Rule

Modules produce bounded facts, the Brain makes the product decisions, and the
UI displays only Brain-approved results. Stale or incomplete source data is not
silently treated as fact. Where terminal evidence remains close or uncertain,
the controller model can fail safe instead of relying on one guessed source.

## Update Notifications

XVatsim performs notify-only update checks against the public JSON manifest at
`https://ezsimulations.github.io/XVatsim/xvatsim_update.json`. When a newer
version is available, the plugin directs the pilot to X-Plane.org or the
official GitHub Release. The in-sim plugin never downloads or installs an
update while X-Plane is running. The standalone V2.2.0 Manager can download a
compatible payload only after verifying its public release facts and every
managed file, and installation remains a user-selected action.

## Release Verification

The V2.2.0 source was configured and built from a fresh Release directory on
2026-10-22. All `911` environment-independent saved regression scenarios
passed; one absolute-path fixture tied to a different local xPilot installation
was classified as host-specific and excluded. The xPilot 4 companion passed
`21 / 21` probes and the Manager passed `40 / 40` probes. Product Owner live
tests passed for nested UK terminal controllers, regional Standby Assist,
route-scoped controller processing, bounded diagnostics, Manager update, and
battery-to-GPU-to-APU electrical transitions.

The final ZIP was independently extracted and contains exactly 12 approved
files. All 11 entries in `SHA256SUMS.txt` passed, the Manager embedded-payload
self-test passed, and the packaged Manager and plugin are byte-identical to the
approved artifacts. The package contains no tests, logs, source files, backups,
symbols, or build artifacts. The ten-page V2.2.0 user guide was rendered and
visually reviewed.

## Repository Layout

- `SDK/`: local X-Plane Plugin SDK
- `brain/`: product decisions and overlay view-model orchestration
- `core/`: workflow, route grammar, route resolution, and route traversal
- `modules/`: isolated live source and task modules
- `plugin/`: in-sim X-Plane plugin host and lifecycle integration
- `tools/regression_harness/`: saved scenario harness for real-world failures
- `tools/release_gate/`: repeatable build, guide, and package tooling
- `docs/`: architecture, update manifest, and public user guide
- `assets/`: package assets such as transition audio and authority registry
- `releases/`: generated public release packages

## Build

From PowerShell:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --fresh -S '.' -B '.\build' -G 'Visual Studio 18 2026' -A x64 -DXPLANE_SDK_ROOT='.\SDK' '-DCMAKE_CXX_FLAGS=/D_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS /EHsc'

& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build '.\build' --config Release --target XVatsimRegressionHarness XVatsimPlugin
```

Run all saved regression scenarios:

```powershell
Get-ChildItem '.\tools\regression_harness\scenarios\*.scn' | Sort-Object Name | ForEach-Object { & '.\build\tools\XVatsimRegressionHarness.exe' $_.FullName; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE } }
```
