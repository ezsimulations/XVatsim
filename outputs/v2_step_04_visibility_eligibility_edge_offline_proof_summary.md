# Step 4 Visibility-Eligibility Edge — Offline Proof Summary

Date: 2026-08-29

## Result

`STEP 4 VISIBILITY-ELIGIBILITY EDGE AND FIRST-VISIBLE-FRAME ACCOUNTING CORRECTION — OFFLINE-PROVEN, NOT CONTROLLED-LIVE-PROVEN`

## Locked start

- Branch `v2-development`; HEAD `03c29a83d5e693b2e51b5f2463393f3df7ad7cc3`.
- Modified/staged/untracked: `6/0/587`.
- Scenarios: `717`; fingerprint `C2D35F643D5ABA133D70ECDE8CBA3232D5A405A37B95EF31CADC3DA586DE0D22`.
- X-Plane/xPilot `0/0`; active/staged `.xpl` `0/0`; active `win_x64` absent.
- Protected `V2 Test` `50/29`; protected evidence locks had zero mismatches.

## Red reproduction

Against the unchanged dirty-coupled transition, corrective scenarios passed
`4/20` and failed `16/20`. The live clean-texture startup failure was reproduced
behaviorally.

## Offline proof

- Visibility-edge corrective: `20/20`.
- Hardened hidden-publication: `17/17`.
- Accessory-input boundary: `26/26`.
- Brain-exclusive accessory: `44/44`.
- Step 3 focused: `31/31`.
- Step 4 focused: `243/243`.
- Complete saved regression: `737/737`.
- Lineage: `717 + 20`; fingerprint `DF4D324783F0DB6BC05FDD48911775AB0AA4F33926DDA2EB1CEAA87CF566631B`.
- 1,000-action stress: issued/terminal `1000/1000`, drops `0`, pending `0`, maximum click/command terminal `100 µs`.
- Warm idle: `100,000` cycles, zero recurring input, preparation, raster, upload, publication, or diagnostic work.
- Worker cancellation maximum: `15 ms`; WinHTTP loopback: `14 ms`.
- Visual proof: 43 PNGs rendered twice, zero PNG hash differences.
- Normal fixture isolation: fixture option OFF and live-fixture marker absent.

## Production-path startup ledger

Hidden command-terminal facts `1`; visibility requests `1`; eligibility edges
`1`; visible attempts `1`; qualifying draws `1`; attempt-terminal facts `1`;
binding consumptions `1`; Brain acceptances `1`; diagnostic serializations `1`.
Command duplicates `0`; attempt duplicates `0`; liveness failures `0`; clicks
`0`; VATSIM requests `0`. Visibility epoch `1`, attempt identity `1`, and
visible-eligibility-to-first-frame `100 µs`. Ten unchanged frames create zero
additional attempts, terminal facts, raster work, or uploads.

## Fixture-off payload

- `build/v2-step4-accessory-input-boundary-release-normal/dist/XVatsim/win_x64/XVatsim.xpl`: `2,297,856` bytes, SHA-256 `5607E43A732418D0D75DDB63C4745D6F85D74FD894A5C6F39252976166E6A325`.
- `authority_source_registry.json`: `40,340` bytes, SHA-256 `3676CA43E5AFB8A5E443FDE6D04616918029E004E17EFAA022695D91E23DB60B`.
- `ui_transition.mp3`: `27,116` bytes, SHA-256 `C7BBE97DAD356C68FDFE40F9E8C1CF4EADEBCCD125463214F963F66356F8D9F1`.

No application was started, no payload was deployed or staged, and no live
request was made. A separate controlled-live reproof gate is recommended but
is not authorized by this result.
