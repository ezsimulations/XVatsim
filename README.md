# XVatsim

XVatsim is a Windows/X-Plane 12 companion plugin for xPilot. It provides a route-aware
cockpit overlay for VATSIM controller awareness, focused on IFR flight-plan operations.

## Current Release

XVatsim V1.0.0 is closed as a verified freeware Windows release for X-Plane 12
and xPilot.

- Freeware package:
  `releases/XVatsim_1.0.0_Freeware_Windows_XP12.zip`
- Package SHA-256:
  `C5F5B9513D2EE1783E1FE40B0E4BFD6331E11F24A8BF5F4CE2F93917815BDF7C`
- Packaged runtime SHA-256:
  `81CC5DD85D579A89257670F51A0F477EAE825F5D78A9F360FBF2AE1979EEF96A`
- User guide:
  `docs/user_guide/XVatsim_User_Guide.pdf`

The X-Plane.org Store submission path is no longer the active release path for
V1.0.0 because the store requested a Mac build. XVatsim V1.0.0 is being released
as freeware instead.

## Current V1 Scope

- Windows
- X-Plane 12
- xPilot
- IFR flight-plan workflow
- Departure, Enroute, and Arrival controller board ownership
- Route-aware center selection using typed route parsing and authority catalogs
- COM1/COM2, TX/RX, MODE C, and Standby Assist status display

## Reliability Rule

XVatsim should fail closed rather than invent truth. Stale feeds, unmatched plans,
missing authority data, invalid aircraft/radio state, and malformed persisted settings
must not be papered over with guessed substitutions.

## Repository Layout

- `SDK/`: local X-Plane Plugin SDK
- `brain/`: overlay view-model orchestration and display formatting
- `core/`: workflow, route grammar, route resolution, and route traversal logic
- `modules/`: isolated live source and task modules
- `plugin/`: in-sim X-Plane plugin host and lifecycle integration
- `tools/regression_harness/`: saved scenario harness for real-world failure cases
- `docs/`: current architecture notes
- `assets/`: package assets such as transition audio
- `releases/`: release/checkpoint packaging materials

## V2.0.0 Direction

Future development starts as XVatsim V2.0.0 work. The first planned V2 workstreams
are dedicated VFR implementation and Mac/Linux support. V2 changes still require
the brain-owned runtime contract: modules produce facts, the brain decides, and
the UI displays brain-approved facts.

## Build

From PowerShell:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build '.\build' --config Release --target XVatsimRegressionHarness XVatsimPlugin
```

Run all saved regression scenarios:

```powershell
Get-ChildItem '.\tools\regression_harness\scenarios\*.scn' | Sort-Object Name | ForEach-Object { & '.\build\tools\XVatsimRegressionHarness.exe' $_.FullName; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE } }
```

## Not In V1

- Private-message, PDC, or AUTO_ATC card presentation
- SimBrief import
- Navigraph AIRAC import
- Dedicated VFR workflow
- Mac, Linux, or X-Plane 11 support
- Second-monitor/out-of-sim window mode
