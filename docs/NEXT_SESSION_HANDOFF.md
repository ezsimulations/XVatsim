# XVatsim Next Session Handoff - V2.0.1 Public Release

Updated: 2026-09-11

## Current State

XVatsim V2.0.1 is the current public focused performance maintenance release
for Windows, X-Plane 12, and xPilot. The Product Owner's controlled live test
confirmed that the previously constant 64-80 microsecond settled flight-loop
workload was no longer visible.

The exact verified package is available from X-Plane.org and the `v2.0.1`
GitHub Release. The public `docs/xvatsim_update.json` reports V2.0.1.

The governing architecture remains:

`Brain decides. Modules produce bounded mechanical facts. UI displays brain-approved facts.`

## V2.0.1 Proof

- Accepted correction commit:
  `0c626a99dfd74399eaed84e3d1e7a93b71ab7c5e`.
- Fresh Visual Studio 18 x64 Release configuration and build: PASS.
- Saved regression: `884 / 884` PASS.
- User-guide PDF render and page-by-page visual review: PASS, nine pages.
- Customer package smoke extraction: PASS, exactly nine files.
- Forbidden tests, logs, backups, source, symbols, and build artifacts: `0`.
- Packaged plugin matches the fresh Release build byte-for-byte.

## Release Artifacts

- Package:
  `C:\Users\DARRON\OneDrive\Documents\XVatsim\releases\XVatsim_2.0.1_Freeware_Windows_XP12.zip`
- Package size: `1986566` bytes
- Package SHA-256:
  `1324EF4B851B6467F00A6A1DBCC96354EF165D7AC3AD80A01780503A130AE928`
- Packaged plugin SHA-256:
  `8CD354F954B1730BF869D789C079A29F6C5531FF81B25211DA1E7F2EEBD671FA`
- Regression: `884 / 884`
- Customer payload: exactly nine files

## Post-Publication State

- X-Plane.org package: published and Product Owner confirmed.
- GitHub tag and Release: `v2.0.1`.
- GitHub Pages manifest: V2.0.1, notify-only, non-critical.
- Offline V2.0.0-to-V2.0.1 notice: PASS.
- Offline V2.0.1 current-version silent behavior: PASS.

## Deferred Direction

X-Plane SDK 4.4 Panel Graphics research is recorded in
`docs/XPLANE_SDK_4_4_PANEL_GRAPHICS_DIRECTION.md`. It is not part of V2.0.1 and
must enter through a separate renderer milestone.
