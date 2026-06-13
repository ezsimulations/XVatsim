# XVatsim V1.0.3 Patch Closeout

Date locked: 2026-06-13

Status: release package built and update manifest prepared.

## Release

XVatsim V1.0.3 is a freeware Windows/X-Plane 12/xPilot patch release.

- Package:
  `releases\XVatsim_1.0.3_Freeware_Windows_XP12.zip`
- Package SHA-256:
  `F974CF81FE73CF18C95C99D2638E64F853C60AEF5FD16CD6B90FA701C4CBC1C1`
- Package size:
  `2411912`
- Packaged runtime SHA-256:
  `8FF264BD52690EC51DA0E5D22C95438CE637198C4A5B00937D3C0B000FFF5645`

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
- Freeware package builder passed:
  `New-FreewareReleasePackage.ps1 -Version 1.0.3`.

## Notes

The package was built locally for upload to the X-Plane.org freeware file page.
The public update manifest points at the same X-Plane.org page and is
notify-only; XVatsim does not download, install, replace, or launch a browser.

The Markdown user guide source was updated to Version 1.0.3. The existing PDF
asset remains the packaged guide until a PDF generation workflow is added or
the PDF is manually regenerated.
