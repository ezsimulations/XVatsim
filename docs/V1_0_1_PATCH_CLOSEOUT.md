# XVatsim V1.0.1 Patch Closeout

Date: 2026-06-03

## Release Decision

XVatsim V1.0.1 is a freeware Windows/X-Plane 12/xPilot patch release.

This release fixes a Version 1 runtime false-rejection class where valid arrival
APP/DEP/TRACON service could be hidden when SimAware terminal boundaries did not
explicitly separate APP and DEP service. The fix stays inside the clean
Brain/module boundary: the Terminal Authority module produces endpoint facts,
the brain-owned relevance path accepts or rejects radio-board candidates, and
the plugin remains the X-Plane shell.

## Release Package

- Package zip:
  `releases/XVatsim_1.0.1_Freeware_Windows_XP12.zip`
- Package SHA-256:
  `E97B787F14B1E0FC5879B5D53D71BD97A1F18ED004E017B2DE1F4955E24ACF78`
- Packaged plugin SHA-256:
  `763491DB96FDFA03FB433A17C4001121BD4A94B137D4CCA4D20C2051BADFA152`
- User guide PDF SHA-256:
  `D1047E600D3A7E637DE61B257CA49FE40D8EA45B729940B12F5D8049D599CAA1`

Package contents:

- `Resources/plugins/XVatsim/win_x64/XVatsim.xpl`
- `Resources/plugins/XVatsim/win_x64/ui_transition.mp3`
- `Resources/plugins/XVatsim/win_x64/authority_source_registry.json`
- `XVatsim_User_Guide.pdf`
- `README.txt`
- `QUICK_START.txt`
- `FREEWARE_LICENSE.txt`
- `CHANGELOG.txt`
- `SUPPORT.txt`

The expanded package folder under `releases/` and the zip are generated
artifacts. The package builder is:

- `tools/release_gate/New-FreewareReleasePackage.ps1`

## Runtime Fix

- `modules/terminal_authority` now treats SimAware TRACON source records with no
  explicit service suffix as shared APP/DEP authority facts only when the
  original SimAware source record proves that the suffix was truly absent.
- Explicit SimAware `APP` or `DEP` suffixes remain explicit and are not
  broadened.
- Missing source records do not broaden authority; the runtime remains
  fail-closed.

Regression cases added:

- `brain_controller_relevance_arrival_simaware_shared_tracon_accepts_kewr_dep.scn`
- `brain_controller_relevance_arrival_simaware_shared_tracon_accepts_kpvd_app.scn`

## Validation

- Release build passed for `XVatsimRegressionHarness` and `XVatsimPlugin`.
- Focused authority regression set passed: `7 / 7`.
- Full regression harness passed: `253 / 253`.
- Freeware package generated successfully.
- Freeware zip smoke extraction passed.
- Package file set verified.
- Package text scan found no store-submission, serial-key, or
  proof-of-purchase wording.
- Package scan found no `.pdb`, `.lib`, `.exp`, `.log`, temp, desktop, or build
  output folder artifacts.
- Packaged plugin hash matched the verified Release runtime hash.
- Installed simulator plugin was updated and verified to match the Release
  build.

Installed X-Plane plugin:

- `C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl`
- Installed SHA-256:
  `763491DB96FDFA03FB433A17C4001121BD4A94B137D4CCA4D20C2051BADFA152`
- Previous installed build backup:
  `C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl.bak-20260603-102134-7AF0EDE64524`

## Live-Style Probe

Darron provided a simulated ENZV arrival radio-board test using live controllers
online at the time. XVatsim's current logic correctly accepted `ENZV_APP
119.605` and rejected nearby `ENBR_D_APP 118.855` and `ENBR_W_APP 121.005` as
arrival terminal-owner mismatches for ENZV. Darron confirmed the decision result
was accurate.

## Next Version

V1.0.1 remains Version 1 IFR freeware scope.

Future product work starts as XVatsim V2.0.0 with:

- dedicated VFR implementation
- Mac support
- Linux support

V2 work must begin with a new Contract Gate. The active runtime contract remains:
Brain decides, modules produce facts, and UI displays brain-approved facts.
