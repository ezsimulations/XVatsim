# XVatsim V2.0.1 Performance Patch Release

Status: Published and release-verified

Published: 2026-09-11

## Purpose

V2.0.1 is a focused maintenance release for the continuous flight-loop work
observed after the V2.0.0 public release. The Product Owner observed XVatsim
remaining near 64 microseconds and periodically reaching the 80-microsecond
range after initial flight preparation should have settled. This could have a
more visible effect on lower-end systems.

The accepted correction is commit
`0c626a99dfd74399eaed84e3d1e7a93b71ab7c5e` (`perf: calm settled operational
refresh work`). A controlled online test confirmed the constant workload was
no longer visible.

## Included Correction

- The Brain owns a settled operational refresh gate.
- Meaningful VATSIM, controller, radio, presentation, PDC, air/ground,
  activation, and worker-state changes wake the complete operational path.
- Unchanged callbacks take a lightweight path.
- A one-second safety refresh remains active.
- Inactive vNAS evidence outside the supported United States scope is reused
  instead of repeatedly scanning the radio board.
- Diagnostic job strings are constructed only for scheduled diagnostic frames.

Controller-selection policy, TRACON evidence, ATIS identity handling,
information ORBs, Standby Assist, UI layout, and renderer behavior are
unchanged.

## Release Proof

- Product Owner controlled live performance test: PASS.
- Fresh Visual Studio 18 x64 Release configure and build: PASS.
- Saved regression scenarios: `884 / 884` PASS, including both V2.0.1 update
  manifest behaviors.
- User-guide PDF generation and nine-page visual review: PASS.
- Customer package smoke extraction: PASS, exactly nine files.
- Forbidden tests, logs, backups, source, symbols, and build artifacts: `0`.
- Packaged plugin matches the fresh Release build byte-for-byte.

## Release Artifacts

- Archive:
  `C:\Users\DARRON\OneDrive\Documents\XVatsim\releases\XVatsim_2.0.1_Freeware_Windows_XP12.zip`
- Extracted package directory:
  `C:\Users\DARRON\OneDrive\Documents\XVatsim\releases\XVatsim_2.0.1_Freeware_Windows_XP12`
- Package size: `1986566` bytes
- Package SHA-256:
  `1324EF4B851B6467F00A6A1DBCC96354EF165D7AC3AD80A01780503A130AE928`
- Packaged plugin SHA-256:
  `8CD354F954B1730BF869D789C079A29F6C5531FF81B25211DA1E7F2EEBD671FA`

## Customer Package Boundary

The archive contains one top-level
`XVatsim_2.0.1_Freeware_Windows_XP12` directory and exactly these nine files:

1. `README.txt`
2. `QUICK_START.txt`
3. `FREEWARE_LICENSE.txt`
4. `CHANGELOG.txt`
5. `SUPPORT.txt`
6. `XVatsim_User_Guide.pdf`
7. `Resources/plugins/XVatsim/win_x64/XVatsim.xpl`
8. `Resources/plugins/XVatsim/win_x64/ui_transition.mp3`
9. `Resources/plugins/XVatsim/win_x64/authority_source_registry.json`

## Publication State

The Product Owner confirmed the exact V2.0.1 archive is live on X-Plane.org.
The matching `v2.0.1` tag and GitHub Release carry that archive, and the public
`docs/xvatsim_update.json` manifest reports V2.0.1 with the recorded size and
hashes. The update remains notify-only and non-critical.

## Deferred SDK Direction

V2.0.1 continues using the proven existing overlay renderer. The X-Plane SDK
4.4 Panel Graphics migration is documented in
`docs/XPLANE_SDK_4_4_PANEL_GRAPHICS_DIRECTION.md` as a separate future
milestone with its own compatibility and performance gates.
