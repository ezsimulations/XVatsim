# XVatsim

XVatsim is a Windows/X-Plane 12 companion plugin for xPilot. It provides a route-aware
cockpit overlay for VATSIM controller awareness, focused on IFR flight-plan operations.

## Current Release

XVatsim V1.2.2 is the current freeware Windows release for X-Plane 12 and
xPilot.

- Freeware package:
  `releases/XVatsim_1.2.2_Freeware_Windows_XP12.zip`
- Package SHA-256:
  `A329CAF1AE589A828421A78DE7CE3B15645492B04CDD983A122A4E5CAC01834F`
- Packaged runtime SHA-256:
  `95DDA79E9C6948A06DE2D3D9920E72D677D0C7E819386656EEA78E32A5E7D06E`
- User guide:
  `docs/user_guide/XVatsim_User_Guide.pdf`

The X-Plane.org Store submission path is no longer the active release path for
Version 1 because the store requested a Mac build. XVatsim Version 1 is being
released as freeware instead.

V1.2.2 is a maintenance release on top of the brain-owned authority recovery
arc. It fixes live-tested arrival Center ordering, prevents tuned stale Centers
from overriding route polygon ownership, contains overlay hot-path stalls, reuses
authority proof cache data across safe route-window transitions, and preserves
the brain-owned authority, display-order, and standby-assist guardrails from
1.2.0 and 1.2.1.

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
