# XVatsim V2.0.2 Controller and PDC Maintenance Release

Release date: 17 September 2026

Baseline: V2.0.1 commit `e34baf1912a112812e85c1940c2031bb0b922a7c`

Validated repair commit:
`987279e12629945d891bc63fcb3c35e13c98e252`

## Release purpose

V2.0.2 restores Brain-owned controller decisions, adds declared VATSIM
coverage-extension evidence for Australia, and replaces exact controller
callsign rejection with simple yes, no, and neutral evidence votes. Workers
report mechanical facts; the Brain alone decides whether a controller is
displayed.

The release also monitors xPilot network logs every five seconds for incoming
direct private and PDC/ACARS messages. The Brain excludes public radio,
broadcast, server, outgoing, other-session, and other-callsign traffic. The PDC
Drawer stores admitted messages newest first, shows amber `NEW` for unread
content, and returns to cyan `IDLE` after the pilot leaves the opened Drawer.

Routine flight-loop timing is reduced to one statistical diagnostic summary per
minute. Rate-limited outlier detail and complete change-driven Brain receipts
remain available for bug and performance investigations.

## Validation

- Product Owner online full-flight test: PASS.
- Saved regression scenarios: `884 / 884` PASS.
- Australia Brain evidence gate: PASS.
- PDC xPilot log monitor gate: PASS.
- Calm flight-loop performance gate: PASS.
- Runtime activation gate: PASS.
- Fresh Visual Studio Release build: PASS.
- Nine-file package contract: PASS.
- Packaged plugin byte identity with the tested Release build: PASS.
- V2.0.2 user guide render review: PASS, nine pages.

## Package

Release archive:

`C:\Users\DARRON\OneDrive\Documents\XVatsim\releases\XVatsim_2.0.2_Freeware_Windows_XP12.zip`

- Archive size: `2042321` bytes
- Archive SHA-256:
  `8BD0BE5137D2844AC64CA1FE444E05D12C43A3C6BEBCFC4CF6370B5B1A8596E9`
- Packaged plugin SHA-256:
  `31F0D5EC3766C662A474A4E113464F956AC312FB61F002130FFAE258B71FA726`
- User guide SHA-256:
  `4673262F369A49FCE866EFC41A20355EF1EE45E5B762B133C04426113DDB2D1B`

The archive contains one top-level
`XVatsim_2.0.2_Freeware_Windows_XP12` directory and exactly these nine files:

1. `README.txt`
2. `QUICK_START.txt`
3. `FREEWARE_LICENSE.txt`
4. `CHANGELOG.txt`
5. `SUPPORT.txt`
6. `XVatsim_User_Guide.pdf`
7. `Resources/plugins/XVatsim/win_x64/XVatsim.xpl`
8. `Resources/plugins/XVatsim/win_x64/ui_transition.mp3`
9. `Resources/plugins/XVatsim/win_x64/authority_source_registry.json`

## Publication order

1. Upload the exact verified archive to X-Plane.org.
2. Obtain Product Owner confirmation that the V2.0.2 X-Plane.org download is
   available.
3. Update the public README and publish the matching GitHub `v2.0.2` release
   with the exact verified archive.
4. Update `docs/xvatsim_update.json` last, using the recorded archive and plugin
   size/hash values, so users are never notified before both download
   destinations are ready.
5. Verify the public notify-only update behavior and reconcile the release tag,
   branch, and repository state.

Until step 4, the public update manifest intentionally remains at V2.0.1.
