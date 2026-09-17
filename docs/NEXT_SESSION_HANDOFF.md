# XVatsim Next Session Handoff - V2.0.2 Public Release

Updated: 2026-09-17

## Current State

XVatsim V2.0.2 is the current public maintenance release for Windows, X-Plane
12, and xPilot. The Product Owner's full online flight test accepted the
Brain-owned controller voting, Australian callsign and coverage-extension
evidence, PDC/private-message Drawer, diagnostic-volume reduction, and runtime
performance.

The exact verified package is available from X-Plane.org and the `v2.0.2`
GitHub Release. The public `docs/xvatsim_update.json` reports V2.0.2.

The governing architecture remains:

`Brain decides. Modules produce bounded mechanical facts. UI displays brain-approved facts.`

## V2.0.2 Proof

- Validated repair commit:
  `987279e12629945d891bc63fcb3c35e13c98e252`.
- Fresh Visual Studio 18 x64 Release configuration and build: PASS.
- Saved regression: `886 / 886` PASS.
- Australia Brain evidence gate: PASS.
- PDC xPilot log monitor gate: PASS.
- Calm flight-loop performance and runtime activation gates: PASS.
- User-guide PDF render and page-by-page visual review: PASS, nine pages.
- Customer package inspection: PASS, exactly nine files.
- Packaged plugin matches the fresh Release build byte-for-byte.

## Release Artifacts

- Package:
  `C:\Users\DARRON\OneDrive\Documents\XVatsim\releases\XVatsim_2.0.2_Freeware_Windows_XP12.zip`
- Package size: `2042321` bytes
- Package SHA-256:
  `8BD0BE5137D2844AC64CA1FE444E05D12C43A3C6BEBCFC4CF6370B5B1A8596E9`
- Packaged plugin SHA-256:
  `31F0D5EC3766C662A474A4E113464F956AC312FB61F002130FFAE258B71FA726`
- Regression: `886 / 886`
- Customer payload: exactly nine files

## Publication State

- X-Plane.org package: published and Product Owner confirmed.
- GitHub tag and Release: `v2.0.2`.
- GitHub Pages manifest: V2.0.2, notify-only, non-critical.
- Offline V2.0.1-to-V2.0.2 notice: PASS.
- Offline V2.0.2 current-version silent behavior: PASS.

## Deferred Direction

X-Plane SDK 4.4 Panel Graphics research remains recorded in
`docs/XPLANE_SDK_4_4_PANEL_GRAPHICS_DIRECTION.md`. It is not part of V2.0.2 and
must enter through a separate renderer milestone.
