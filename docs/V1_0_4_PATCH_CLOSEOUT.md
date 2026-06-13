# XVatsim V1.0.4 Patch Closeout

Status: packaged for freeware release.

XVatsim V1.0.4 is a Windows/X-Plane 12/xPilot freeware patch release.

## Package

- ZIP:
  `releases\XVatsim_1.0.4_Freeware_Windows_XP12.zip`
- ZIP size:
  `1409597`
- ZIP SHA-256:
  `7D215C941192E85133FD2CE69B6F87E808C21F5DDB53ED48B7B31A0950A5CF39`
- Packaged plugin SHA-256:
  `58BC6C9AAAB3CB1E4DDFEE3DAF16BBD7655C3E670A3EE1C78BB0C28F3FECD07B`
- User guide PDF SHA-256:
  `422B0676B592EC253223D156100AD855C2C120B7341AECF5AF3E1A2840266F51`

## Fixes

- Added a model-driven top-right overlay version chip below the phase text.
- Green chip means the installed version is current.
- Gray chip means update status is unknown.
- Amber rotating installed-version/`UPDATE` chip means a newer build is available.
- Replaced clipped footer update text with a dismissible update notice panel.
- Manual update checks now use the same notice panel for available, current,
  in-progress, and failed states.
- Automatic update checks now run once per simulator/plugin session; an old
  persisted check timestamp no longer suppresses startup discovery of a newly
  published manifest.
- Retained V1.0.3 weighted brain-owned terminal controller relevance fixes.

## Verification

- `RelWithDebInfo` build passed.
- Release build passed after sanitizing the duplicate `Path`/`PATH` environment
  variable for the build process.
- Targeted update manifest scenarios passed:
  - available update: installed `V1.0.3`, manifest `V1.0.4`, rotating update
    chip, visible notice panel.
  - current version: installed `V1.0.4`, manifest `V1.0.4`, green chip, no
    automatic notice.
  - malformed manifest: failed closed, no false available notice.
- Full regression harness passed after `RelWithDebInfo` build.
- Full regression harness passed after Release build.
- User guide PDF text verified for `Version 1.0.4` and update notice language.
