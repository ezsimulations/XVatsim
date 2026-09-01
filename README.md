# XVatsim

XVatsim is a Windows/X-Plane 12 companion plugin for xPilot. It provides a route-aware
cockpit overlay for VATSIM controller awareness, focused on IFR flight-plan operations.

## Current Release

XVatsim V1.2.3 is the current freeware Windows release for X-Plane 12 and
xPilot.

- Freeware package:
  `releases/XVatsim_1.2.3_Freeware_Windows_XP12.zip`
- Package SHA-256:
  `80B013ADB454D6F55AD359825E7E3229BD85C12A146289B4D17A15894049497C`
- Packaged runtime SHA-256:
  `28896800BAD64A5C25933F828D0D10FD63E0ED8C1AF5471760A4F1E599CFE23C`
- User guide:
  `docs/user_guide/XVatsim_User_Guide.pdf`
- Download pages:
  [X-Plane.org](https://forums.x-plane.org/files/file/100224-xvatsim-100-freeware-windows-xp12zip/)
  or [GitHub Releases](https://github.com/ezsimulations/XVatsim/releases/tag/v1.2.3)

The X-Plane.org Store submission path is no longer the active release path for
Version 1 because the store requested a Mac build. XVatsim Version 1 is being
released as freeware instead.

## V1.2.3 Patch Release

V1.2.3 fixes an exact route-polygon crossing failure found on the live-tested
SKJ914 route from MMTO to KCOS. Route traversal now refines polygon entry across
ordered boundary intervals instead of probing a fixed distance on either side
of a crossing. This restores KZFW route ownership and permits the brain to prove
the relevant FTW Center candidate without changing controller-relevance or
display ownership.

The source/build version, plugin labels, network user agents, regression-harness
default, release tooling, user guide, and public update manifest are set to
1.2.3. The plugin's update notice directs users to X-Plane.org or GitHub.

Release verification completed on 2026-08-15: the Release regression harness
and plugin targets built successfully, eight focused route/update guardrails
passed, all 451 saved regression scenarios passed, and an independent package
smoke extraction passed.

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

## V2.0.0 Feature-Complete Beta

Steps 1 through 6 are accepted, complete, and frozen as the V2.0.0 feature
baseline. The existing main card now has three completed Brain-owned ORBs:
METAR, VATSIM ATIS, and a one-shot xPilot waiting-message PDC drawer.
V2.0.0 remains a Windows/X-Plane 12/xPilot plugin. Mac and Linux support remain
deferred until native proof resources exist.

The PDC feature retains exactly one bounded snapshot: after a complete
Brain-owned flight identity exists, the first stable-connected,
positive-sequence xPilot waiting message with a non-empty bounded body is
captured, and further private-source sampling stops for that flight. The drawer
warns `CAPTURED SNAPSHOT — CHECK XPILOT FOR REVISIONS`; xPilot remains
authoritative for later amendments. XVatsim does not reproduce a general
private-message inbox or classify text by sender, controller roster, wording,
IFR/VFR mode, or workflow stage.

The governing contract remains Rule One: modules report bounded mechanical
facts, the Brain makes every semantic and product decision, the plugin obeys,
and the overlay mechanically renders Brain-approved projections. METAR, ATIS,
and PDC share one accessory snapshot/preparation/presentation/publication
architecture rather than parallel UI systems.

The feature commit is
`94825f07248dafc038d4c29e06ea61e40f49cfc3`; the proof and closeout commit is
`380039a4494f0b373542eced807345471f214036`. The final Step 6 baseline passed
visual `14/14`, focused `61/61`, and complete `861/861` regression twice. Its
canonical scenario fingerprint is
`227121979F59CB3BB359B96DE57AC7DF601E0BE3736F82F38C19EE08437431D9`.
The Product Owner normal-use live proof passed. The exact live-proven candidate
remains installed as the extended-beta baseline with:

- `XVatsim.xpl`: `2,572,288` bytes /
  `CA2840F6FE61124E37881124DF78E4B0A0049FD47C5BF4FA6EE179D9A6E77934`;
- `authority_source_registry.json`: `40,340` bytes /
  `3676CA43E5AFB8A5E443FDE6D04616918029E004E17EFAA022695D91E23DB60B`;
- `ui_transition.mp3`: `27,116` bytes /
  `C7BBE97DAD356C68FDFE40F9E8C1CF4EADEBCCD125463214F963F66356F8D9F1`.

This is not a public V2.0.0 release or release certification.

The accepted IFR/VFR selection foundation remains. A dedicated VFR evidence
engine or live VFR controller model is not a V2.0.0 requirement and is only an
optional Version 3 product decision, with no commitment to build it. Extended
multi-flight beta will monitor bounded startup preparation, including the one
observed nonblocking `1,713 ms` route/authority refresh warning, and verify that
settled operation remains calm and low-churn.

V1.2.3 remains the current public release and all embedded version metadata
remains `1.2.3` until a separately approved beta-packaging gate builds and
smoke-tests different bytes.

## Build

From PowerShell:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build '.\build' --config Release --target XVatsimRegressionHarness XVatsimPlugin
```

If a Visual Studio 18/MSVC 14.51 update requires a fresh CMake configuration,
configure cpprestsdk's legacy coroutine compatibility explicitly:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --fresh -S '.' -B '.\build' -G 'Visual Studio 18 2026' -A x64 -DXPLANE_SDK_ROOT='.\SDK' '-DCMAKE_CXX_FLAGS=/D_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS /EHsc'
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
