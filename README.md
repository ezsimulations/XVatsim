# XVatsim

XVatsim is a Windows/X-Plane 12 companion plugin for xPilot. It provides a route-aware
cockpit overlay for VATSIM controller awareness, focused on IFR flight-plan operations.

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
