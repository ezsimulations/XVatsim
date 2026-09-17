# XVatsim

XVatsim is a Windows/X-Plane 12 companion plugin for xPilot. It provides a
route-aware cockpit overlay for VATSIM controller awareness, focused on IFR
flight-plan operations.

## Current Release

XVatsim V2.0.2 is the current public freeware maintenance release for Windows,
X-Plane 12, and xPilot.

- Freeware package:
  `releases/XVatsim_2.0.2_Freeware_Windows_XP12.zip`
- Package size: `2042321` bytes
- Package SHA-256:
  `8BD0BE5137D2844AC64CA1FE444E05D12C43A3C6BEBCFC4CF6370B5B1A8596E9`
- Packaged plugin SHA-256:
  `31F0D5EC3766C662A474A4E113464F956AC312FB61F002130FFAE258B71FA726`
- User guide:
  `docs/user_guide/XVatsim_User_Guide.pdf`
- Download pages:
  [X-Plane.org](https://forums.x-plane.org/files/file/100224-xvatsim_100_freeware_windows_xp12zip/)
  and [GitHub Releases](https://github.com/ezsimulations/XVatsim/releases/tag/v2.0.2)

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

The PDC feature monitors xPilot's network log at a low five-second cadence after
complete flight identity and a stable xPilot connection are available. The
Brain admits incoming direct messages addressed to the active callsign and
keeps a bounded newest-first history. xPilot remains the authoritative VATSIM
client.

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
- PDC/ACARS and incoming private-message Drawer sourced from xPilot logs

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
official GitHub Release. XVatsim never downloads or installs an update.

## Release Verification

The V2.0.2 source was configured and built from a fresh Release directory on
2026-09-17. All `886 / 886` saved regression scenarios passed. The customer ZIP
was independently extracted and contains exactly nine approved files: the
plugin, transition audio, authority registry, user guide, README, quick start,
freeware license, changelog, and support instructions. It contains no tests,
logs, source files, backups, symbols, or build artifacts. The packaged plugin
is byte-identical to the verified Release build.

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
