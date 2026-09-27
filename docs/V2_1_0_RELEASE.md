# XVatsim V2.1.0 Manager And xPilot 4 Release

Release date: 26 September 2026

Baseline: V2.0.2 commit `b1f968f9854223b99f22e1c8f9f5fdf9bc170b51`

## Release purpose

V2.1.0 adds the standalone XVatsim Manager and the read-only xPilot 4 SDK
companion while preserving the proven xPilot 3 integration. The Manager finds
and validates X-Plane and xPilot installations, installs only approved files,
verifies every managed file by SHA-256, and restores prior files if a
transaction fails.

The xPilot 4 companion consumes facts and events exposed by the official beta
SDK. It does not independently connect to VATSIM. It reports observations to
XVatsim, where the Brain remains the only controller and message decision
maker. The companion accepts the approved SDK 0.1.x family and refuses an
incompatible future SDK before subscribing to events.

The release also recovers retained xPilot 4 PDC/ACARS information at startup,
wraps long PDC Drawer text, advances METAR received-age text, and derives a
useful flight-rules category from ceiling and visibility evidence when the
source category is stale or unknown.

The Manager is distributed as unsigned freeware. Windows SmartScreen may show
an unknown-publisher warning. The package includes plain `More info` and
`Run anyway` instructions and a complete `SHA256SUMS.txt` file.

## Validation

- Product Owner online controller, METAR, PDC, and message tests: PASS.
- Product Owner Manager scenario matrix: PASS.
- Environment-independent saved scenarios: `887 / 887` PASS.
- Focused C++ contract probes: `11 / 11` PASS.
- xPilot 4 real named-pipe stress proof: `15 / 15` consecutive PASS.
- xPilot 4 SDK companion probe: `21 / 21` PASS.
- Manager isolated probe: `24 / 24` PASS.
- SDK API `0.1.99` accepted and API `0.2.0` refused before subscription: PASS.
- Final ZIP clean extraction and all checksum entries: `11 / 11` PASS.
- Extracted Manager embedded-payload self-test: PASS.
- V2.1.0 user-guide render review: PASS, nine pages.

## Package

Release archive:

`XVatsim_2.1.0_Freeware_Windows_XP12.zip`

- Archive size: `64417929` bytes
- Archive SHA-256:
  `85603D6369938DCE471A0954D1C4A659CE14A26E547B29FE893A5ABB22D8E115`
- Packaged Manager SHA-256:
  `8AA67D8039742435B44F695E2C8B5DCB2751DE21C36612AE6C44A7629D3D52E0`
- Packaged plugin SHA-256:
  `0D4ACC2A99309EE3ACC640798C286144044E57595E93AD8F7364485A65CBAEEF`

The archive contains one top-level
`XVatsim_2.1.0_Freeware_Windows_XP12` directory and exactly these 12 files:

1. `README.txt`
2. `QUICK_START.txt`
3. `FREEWARE_LICENSE.txt`
4. `CHANGELOG.txt`
5. `SUPPORT.txt`
6. `WINDOWS_SMARTSCREEN.txt`
7. `SHA256SUMS.txt`
8. `XVatsimManager.exe`
9. `XVatsim_User_Guide.pdf`
10. `Resources/plugins/XVatsim/win_x64/XVatsim.xpl`
11. `Resources/plugins/XVatsim/win_x64/ui_transition.mp3`
12. `Resources/plugins/XVatsim/win_x64/authority_source_registry.json`

## Installation

1. Close X-Plane and xPilot.
2. Extract the ZIP and run `XVatsimManager.exe`.
3. If SmartScreen appears, select `More info`, confirm the application name,
   and select `Run anyway`.
4. Confirm the validated X-Plane and xPilot folders.
5. Select `Install`, `Update`, or `Repair` and wait for the success message.

The included `Resources` folder remains available as a manual X-Plane-only
fallback. It does not install the xPilot 4 companion.
