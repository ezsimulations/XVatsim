# XVatsim V1.0.2 Patch Closeout

Date: 2026-06-08

## Release Position

XVatsim V1.0.2 is the current freeware Windows/X-Plane 12/xPilot release.
This patch remains notify-only and freeware-distributed through the X-Plane.org
file page.

Active freeware package:

- `releases\XVatsim_1.0.2_Freeware_Windows_XP12.zip`
- Package SHA-256:
  `C4F4DD6C7AF60A96DC0840D73CBD8AFEB9602FE8FCAC6B8FA5BFB0E136324ADB`
- Package size:
  `2782092` bytes
- Packaged plugin SHA-256:
  `037430F5BB2BFF346AB8E8621CF9CE30D5E78625E1FD518CB18A09382F4D9F72`
- Installed simulator plugin SHA-256:
  `037430F5BB2BFF346AB8E8621CF9CE30D5E78625E1FD518CB18A09382F4D9F72`
- User guide PDF SHA-256:
  `E449D4BEBAC0FDA0431FBEAF1EDAC9E087A45D3AE9323F9687EB48AC56C2340C`

## Included Fixes

- Fixed missed airway route sector resolution by preferring validated expanded
  FMS route geometry before raw airway expansion.
- Added unresolved-airway diagnostics so failed airway expansion cannot
  collapse silently into an exact direct route.
- Added bounded cached-transceiver holdover so route-relevant enroute
  controllers stay visible during short AFV radio-range refresh gaps.
- Moved final frequency display order, `Active` row ownership, and Standby
  Assist target selection under brain ownership.
- Made COM1 active frequency the only radio state that advances the Standby
  Assist pointer.
- Reduced Assist Module behavior to helper/reporting responsibility only.
- Added notify-only update checks against the public JSON manifest, including
  24-hour automatic cadence and manual `Check for Updates`.
- Updated the public manifest at `docs\xvatsim_update.json`.

## Validation

- RelWithDebInfo plugin and regression harness build passed.
- Focused contract diagnostics passed: `6 / 6`.
- Full regression harness passed: `262 / 262`.
- Stale competing display/assist ordering scan passed.
- Freeware package generated successfully.
- Freeware ZIP smoke extraction passed.
- Package file-set scan found no `.pdb`, `.lib`, `.exp`, `.log`, temp, debug,
  or store-submission files.
- Packaged plugin hash matched the installed simulator plugin hash.
- GitHub Pages served the update manifest successfully.
- The in-sim manual update check reported the installed version as current.
- Darron reported the live simulator test passed before commit/package
  closeout.

## Release Notes

This release keeps the core architecture rule intact: modules produce facts,
the brain decides, and the UI displays brain-approved facts. The radio board
and Standby Assist now follow that rule directly, which prevents module-side or
UI-side ordering from choosing a next-polygon Center ahead of a current-polygon
Center.

The update checker is intentionally notify-only. XVatsim does not download,
install, replace itself, or launch a browser. Users are told to download the
new ZIP from the X-Plane.org file page when a newer manifest version is
published.
