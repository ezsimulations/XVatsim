# XVatsim

XVatsim is a Windows/X-Plane 12 companion plugin for xPilot. It provides a
route-aware cockpit overlay for VATSIM controller awareness, focused on IFR
flight-plan operations.

## Current Release

XVatsim V2.0.0 is the current public freeware release for Windows, X-Plane 12,
and xPilot.

- Freeware package:
  `releases/XVatsim_2.0.0_Freeware_Windows_XP12.zip`
- Package size: `1982428` bytes
- Package SHA-256:
  `FFA1BE32734BA20F7E6045F9ECEE5AE27D4929927E93965EB6959A33F9E7876B`
- Packaged plugin SHA-256:
  `C9E687E4D1430C4BD730BCF6F20F66E3A47277F27D130863E72FEBDCE188A2AB`
- User guide:
  `docs/user_guide/XVatsim_User_Guide.pdf`
- Download pages:
  [X-Plane.org](https://forums.x-plane.org/files/file/100224-xvatsim-100-freeware-windows-xp12zip/)
  or [GitHub Releases](https://github.com/ezsimulations/XVatsim/releases/tag/v2.0.0)

## Version 2.0.0 Highlights

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

The PDC feature retains exactly one bounded snapshot after complete flight
identity and a stable xPilot connection are available. The drawer warns
`CAPTURED SNAPSHOT — CHECK XPILOT FOR REVISIONS`; xPilot remains authoritative
for amendments and later messages. XVatsim is not a general private-message
inbox and does not classify a message by sender or wording.

## Current Scope

- Windows
- X-Plane 12
- xPilot
- IFR flight-plan controller workflow
- Departure, Enroute, and Arrival controller-board ownership
- Route-aware center selection using typed route parsing and authority catalogs
- US terminal-sector confirmation using vNAS data
- COM1/COM2, TX/RX, Mode C, and Standby Assist status
- VATSIM METAR and ATIS information drawers
- One-shot PDC convenience snapshot from xPilot

Not included:

- Mac, Linux, or X-Plane 11 support
- A dedicated VFR controller-evidence workflow
- SimBrief or Navigraph AIRAC import
- A general private-message inbox or AUTO_ATC card
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
official GitHub Release. XVatsim never downloads or installs an update.

## Release Verification

The V2.0.0 source was configured and built from a fresh Release directory on
2026-09-08. All `882 / 882` saved regression scenarios passed. The customer ZIP
was independently extracted and contains exactly nine approved files: the
plugin, transition audio, authority registry, user guide, README, quick start,
freeware license, changelog, and support instructions. It contains no tests,
logs, source files, backups, symbols, or build artifacts.

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
