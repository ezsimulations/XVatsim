# XVatsim V1.0.0 Freeware Closeout

Date: 2026-06-02

## Release Decision

XVatsim V1.0.0 is closed as a freeware Windows/X-Plane 12/xPilot plugin release.

The X-Plane.org Store submission path is no longer active for V1.0.0 because the
store requested a Mac version. Rather than delay the Windows release for a new
platform port, V1.0.0 is being released as freeware.

## Release Package

- Package zip:
  `releases/XVatsim_1.0.0_Freeware_Windows_XP12.zip`
- Package SHA-256:
  `C5F5B9513D2EE1783E1FE40B0E4BFD6331E11F24A8BF5F4CE2F93917815BDF7C`
- Packaged plugin SHA-256:
  `81CC5DD85D579A89257670F51A0F477EAE825F5D78A9F360FBF2AE1979EEF96A`

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

The expanded package folder under `releases/` is a generated artifact and should
not be committed. The zip is the deliverable artifact.

## Validation

- Fresh Release plugin build passed.
- Freeware zip smoke extraction passed.
- Expected package file set verified.
- Package text scan found no store, commercial, or proof-of-purchase wording.
- Package scan found no `.pdb`, `.lib`, `.exp`, `.log`, temp, debug, or
  store-submission files.
- Packaged plugin hash matched the verified Release runtime hash.
- Darron reported the freeware package passes cleanly.

Diagnostic logging remains part of the runtime by design so customers can send
diagnostics when reporting bugs. Generated diagnostic logs are not shipped inside
the release package.

## User Documentation

The professional user guide is now part of the repo documentation and the
freeware package:

- `docs/user_guide/XVatsim_User_Guide.md`
- `docs/user_guide/XVatsim_User_Guide.pdf`
- `docs/user_guide/assets/*.jpg`

The guide covers installation, normal operation, CTAF/UNICOM behavior, controller
row colors and badges, menu functions, X-Plane keyboard command setup,
long-haul reconnect/recovery, and bug-report diagnostics.

## Runtime Status

V1.0.0 completed the five-flight live battle-test gate for installed runtime hash:

`81CC5DD85D579A89257670F51A0F477EAE825F5D78A9F360FBF2AE1979EEF96A`

No runtime code changed during this closeout.

## Next Version

Future product work starts as XVatsim V2.0.0.

Initial V2 targets:

- dedicated VFR implementation
- Mac support
- Linux support

V2 work must begin with a new Contract Gate. The existing brain-owned runtime
contract remains active: Brain decides, modules produce facts, and UI displays
brain-approved facts.
