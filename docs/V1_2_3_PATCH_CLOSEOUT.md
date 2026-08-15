# XVatsim V1.2.3 Patch Closeout

Closed: 2026-08-15

## Release Outcome

XVatsim V1.2.3 is the current freeware Windows/X-Plane 12/xPilot release.
Users may obtain the package from X-Plane.org or GitHub Releases. The plugin UI
shows `V1.2.3`, and an available-update notice reads
`Download: X-Plane.org or GitHub`.

## Fix

V1.2.3 fixes exact route-polygon crossing detection. Route traversal now
refines polygon entry across ordered boundary intervals rather than probing a
fixed distance on either side of a crossing. This restores KZFW ownership and
the relevant FTW Center candidate on the live-tested SKJ914 MMTO-KCOS route
while preserving brain-owned controller relevance and display ownership.

## Package

- Archive: `releases/XVatsim_1.2.3_Freeware_Windows_XP12.zip`
- Size: `1663402` bytes
- Archive SHA-256:
  `80B013ADB454D6F55AD359825E7E3229BD85C12A146289B4D17A15894049497C`
- Packaged plugin SHA-256:
  `28896800BAD64A5C25933F828D0D10FD63E0ED8C1AF5471760A4F1E599CFE23C`
- GitHub Release:
  `https://github.com/ezsimulations/XVatsim/releases/tag/v1.2.3`
- X-Plane.org:
  `https://forums.x-plane.org/files/file/100224-xvatsim_100_freeware_windows_xp12zip/`

## Verification

- Release plugin and regression-harness builds passed.
- Eight focused route, controller-relevance, and update scenarios passed.
- Full regression passed: `451 / 451`.
- Independent ZIP smoke extraction found the nine required customer files,
  correct V1.2.3 text, and no debug/test artifacts.
- The packaged plugin matches the validated Release build.
- The seven-page user-guide PDF was regenerated, rendered, and visually
  inspected.
- The public JSON manifest records the verified filename, byte count, package
  hash, plugin hash, publication date, and both download destinations.

## Release-Folder Cleanup

Only the V1.2.2 and V1.2.3 package directories and archives remain in
`releases`. Older generated release packages and store-submission kits were
moved to the Windows Recycle Bin and remain recoverable until it is emptied.

## Contract Preservation

`Brain decides. Modules produce facts. UI displays brain-approved facts.`

The route fix remains inside core route geometry. The plugin shell does not
decide route ownership or controller relevance, and the UI only renders the
brain-owned update-notice text.
