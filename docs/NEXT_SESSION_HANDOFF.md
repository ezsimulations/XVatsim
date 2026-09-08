# XVatsim Next Session Handoff — V2.0.0 Public Release

Updated: 2026-09-08

## Current State

XVatsim V2.0.0 is the current public freeware release for Windows, X-Plane 12,
and xPilot. The release follows Product Owner live acceptance of the completed
METAR, VATSIM ATIS, one-shot PDC, performance, US TRACON/vNAS, and ATIS
identical-content work.

The governing architecture remains:

`Brain decides. Modules produce bounded mechanical facts. UI displays brain-approved facts.`

## Release Proof

- Fresh Visual Studio 18 x64 Release configuration and build: PASS.
- Saved regression: `882 / 882` PASS.
- V1.2.3-to-V2.0.0 update notification scenario: PASS.
- V2.0.0 current-version/silent automatic check scenario: PASS.
- User-guide PDF render and page-by-page visual review: PASS, nine pages.
- Customer package smoke extraction: PASS, exactly nine files.
- Forbidden tests, logs, backups, source, symbols, and build artifacts: `0`.
- Packaged plugin matches the fresh Release build.
- Compiled V2.0.0 string hits: `4`; compiled V1.2.3 string hits: `0`.

## Release Artifacts

- Package: `releases/XVatsim_2.0.0_Freeware_Windows_XP12.zip`
- Package size: `1982428` bytes
- Package SHA-256:
  `FFA1BE32734BA20F7E6045F9ECEE5AE27D4929927E93965EB6959A33F9E7876B`
- Packaged plugin SHA-256:
  `C9E687E4D1430C4BD730BCF6F20F66E3A47277F27D130863E72FEBDCE188A2AB`
- GitHub Release:
  `https://github.com/ezsimulations/XVatsim/releases/tag/v2.0.0`
- X-Plane.org:
  `https://forums.x-plane.org/files/file/100224-xvatsim-100-freeware-windows-xp12zip/`
- Update manifest:
  `https://ezsimulations.github.io/XVatsim/xvatsim_update.json`

## Customer Package Boundary

The ZIP contains only:

1. `README.txt`
2. `QUICK_START.txt`
3. `FREEWARE_LICENSE.txt`
4. `CHANGELOG.txt`
5. `SUPPORT.txt`
6. `XVatsim_User_Guide.pdf`
7. `Resources/plugins/XVatsim/win_x64/XVatsim.xpl`
8. `Resources/plugins/XVatsim/win_x64/ui_transition.mp3`
9. `Resources/plugins/XVatsim/win_x64/authority_source_registry.json`

The local X-Plane `V2 Test` directory, diagnostics, logs, development backups,
proof evidence, source, and symbols are not customer payloads.

## Next Work

Begin with a read-only post-release check. Confirm `main`, tag `v2.0.0`, the
GitHub Release asset, the X-Plane.org download, the GitHub Pages manifest, and a
clean repository. Do not change controller logic, source scheduling, package
contents, or the update manifest without a new Product Owner-approved scope.
