# XVatsim V1.0.3 Patch Closeout

Date locked: 2026-06-13

Status: release package built and update manifest prepared.

## Release

XVatsim V1.0.3 is a freeware Windows/X-Plane 12/xPilot patch release.

- Package:
  `releases\XVatsim_1.0.3_Freeware_Windows_XP12.zip`
- Package SHA-256:
  `C81CFE2095BFA2B615D5D273ECE3856DB4ADBA8922950E852D6317D6D2CBA180`
- Package size:
  `1407605`
- Packaged runtime SHA-256:
  `8FF264BD52690EC51DA0E5D22C95438CE637198C4A5B00937D3C0B000FFF5645`
- User guide PDF SHA-256:
  `0D0477751494B86B67642C6CBF7F61EAD4C3DAA68DA2677661C3E754C992B23F`

## Scope

- Added a weighted brain evidence ledger for terminal APP/DEP controller
  relevance.
- Prevented one terminal-owner text mismatch from hiding a controller when
  multiple VATSIM, radio, route, and source-owned authority facts support
  displaying it.
- Kept FAA/NASR airport frequency facts in the brain as low-weight positive
  context only.
- Treated FAA/NASR misses as neutral because VATSIM may use virtual or pseudo
  frequencies.
- Passed scheduled source-owned authority relevance into the clean runtime
  controller relevance request.
- Added terminal decision diagnostics for vote count, weighted score, neutral
  facts, non-FAA evidence families, confidence, and final display/hide action.
- Added visible installed-version text to the overlay and plugin menu.

## Validation

- Release build passed for `XVatsimPlugin` and `XVatsimRegressionHarness`.
- Full regression harness passed:
  `passed=263`.
- User guide PDF regenerated from the V1.0.3 Markdown source with the bundled
  ReportLab runtime.
- Freeware package builder passed after PDF regeneration:
  `New-FreewareReleasePackage.ps1 -Version 1.0.3`.

## Notes

The package was built locally for upload to the X-Plane.org freeware file page.
The public update manifest points at the same X-Plane.org page and is
notify-only; XVatsim does not download, install, replace, or launch a browser.

The Markdown user guide source and packaged PDF both identify Version 1.0.3.
