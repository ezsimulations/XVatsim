# Pre-V3 Legacy/Map Recovery Baseline

This repository state preserves the legacy XVatsim implementation that was
built and active immediately before the V3 test plugin replaced it.

## Installed baseline

- Build time: 2026-10-29 18:38:37 local
- Size: 3,253,248 bytes
- SHA-256: `F97AB3C50A486474A3A72538C99E82232E8B47986357B48F5906605F0547BD2F`
- Build artifact: `build/dist/XVatsim/win_x64/XVatsim.xpl`
- Installed path: `C:\X-Plane 12\Resources\plugins\XVatsim\win_x64\XVatsim.xpl`
- Preserved copy: `C:\X-Plane 12\Resources\plugins-disabled\XVatsim-V2-disabled-20261030\win_x64\XVatsim.xpl`

The build artifact, preserved copy, and installed file were verified as
byte-identical at baseline creation.

## Provenance

The source is the complete legacy working tree based on public V2.2.0 commit
`6b39f7b2351abc2b4c635b4d13650b62e9563d9f`. The Map-integration work had not
been committed before V3 development began. Files in the functional source
tree had no writes after the baseline binary was built.

## Recovery boundary

- V3 source is frozen and is not a recovery source.
- The baseline records the starting point; it does not claim that the Map
  integration or legacy internals are correct.
- Live observation comes before corrective code changes.
- Existing Assist, radio, PDC, ATIS, METAR, ORB, drawer, and xPilot behavior
  remains the behavioral reference while internal defects are investigated.
- Identifier spelling, controller-sector suffixes, provider version text, and
  similar external variations must not invalidate an otherwise coherent
  flight or controller product unless the governing external protocol truly
  requires exact identity.
- Corrections must remove the owning design defect rather than accumulate
  compatibility patches or special-case strings.

