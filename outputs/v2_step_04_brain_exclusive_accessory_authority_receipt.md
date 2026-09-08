# V2 Step 4 Brain-Exclusive Accessory Authority Receipt

Date: 2026-08-28

## Status

`STEP 4 BRAIN-EXCLUSIVE AUTHORITY RESTORED AND OFFLINE-PROVEN — CONTROLLED LIVE REPROOF NOT AUTHORIZED`

## Commits

- Implementation and evidence:
  `90c6d148ac27d7a401b6447429fb2c1927cbdd0d`
  (`fix: restore brain-exclusive accessory authority`).
- This receipt is recorded by the immediately following receipt-only commit.

## Exact correction

- Preserved all three failing pre-correction red reproductions.
- Replaced worker-side VATSIM acceptance with a stateless payload-only decoder
  and one composite transport/decode fact returned by the existing worker.
- Kept station match, report acceptance, parsing authorization, category/cache/
  history/source-health decisions, presentation, and scheduling exclusively in
  the brain.
- Added one immutable brain accessory command with exact command/lifecycle
  identity and brain-owned rail/drawer revisions.
- Invalidated stale hidden preparations by exact command, content, history,
  drawer revision, layout, and lifecycle identity.
- Removed draw-time presentation commitment; `OverlayWindow::UpdateAccessory`
  is the only production commit coordinator.
- Removed render acknowledgement from pilot-input eligibility.
- Added one truthful terminal mechanical fact per issued command, including
  no-pixel commit, exact first display, both supersession phases, exact-stage
  failure, and lifecycle cancellation.
- Reserved terminal-result capacity before command issue; no terminal result is
  overwritten, coalesced, or silently dropped.
- Preserved full elapsed liveness accounting through cancellation.

## Regression

- Brain-exclusive correction: 44/44.
- Step 3 focused: 31/31.
- Complete Step 4 focused: 180/180.
- Complete saved suite: 674/674.
- Canonical scenario fingerprint:
  `EE5B15EAE725EBA23AEA1CE9F9FEB82D0D93578C0A18EBA6129AE8B898E645C3`.

## Stress, idle, and lifecycle

- 1,000 actions: 1,000 issued/terminal, zero dropped, zero queued, zero pending
  publication, no behavioral in-flight state, 100 us deterministic maximum,
  zero liveness failures.
- 100,000 warm unchanged cycles: zero history traversal/copy, preparation,
  wrapping, raster, upload, publication, input dispatch, or diagnostic work.
- Cooperative worker cancellation: five phases, 15 ms maximum, 500 ms limit.
- Stalled loopback cancellation observation: 0 ms.
- Cancelled commands preserve elapsed time and remain subject to the 500 ms
  liveness classification.

## Transport regression

- Real WinHTTP loopback lifecycle: passed.
- Exact request path: `/KDFW?format=json`.
- Loopback success elapsed: 17 ms.
- Worker harvest: one composite transport/decode fact.
- Brain parse: once.
- Non-200, malformed JSON, and stalled cancellation cases: passed.
- Production endpoint, callback sequence, timeout, cancellation, and bounds:
  unchanged.

## Visual proof

- Production-raster PNGs: 33.
- Repeated render PNGs: 33.
- PNG differences: 0.
- Main-card production signature: unchanged.
- Primary pixel changes: exactly one rail raster and one rail upload.
- KABQ spotlight with unchanged primary ORB: zero rail raster/upload.
- Identical primary content: zero rail/drawer raster/upload.

## Fresh Windows Release payload

- Plugin bytes: 2,422,784.
- Plugin SHA-256:
  `F6C5B2B35CBBDB83BCE1FBD6D9A8029F3E7A258BFC4B420F8F87A208FCFCB13C`.
- Registry SHA-256:
  `3676CA43E5AFB8A5E443FDE6D04616918029E004E17EFAA022695D91E23DB60B`.
- Transition audio SHA-256:
  `C7BBE97DAD356C68FDFE40F9E8C1CF4EADEBCCD125463214F963F66356F8D9F1`.
- Required `metar.vatsim.net` and `VATSIM_METAR` markers: present.
- NOAA, Aviation Weather, loopback, synthetic, injected transport,
  proof-endpoint, scenario, and proof-fixture markers: absent.

## Evidence protection and external state

- Original protected evidence: 116/116 hash rows verified, zero mismatches.
- First failed-live evidence: 34 physical files, manifest unchanged and valid.
- Second corrected-live evidence: 46 physical files/45 manifest rows, valid.
- Prior protected rows: 196/196 verified.
- Accessory-liveness controlled-live evidence: 39/38 manifest rows, valid.
- Latest ORB-publication controlled-live evidence: 51/50 manifest rows, valid;
  manifest SHA-256 remains
  `DA1A0BC736E73044F9EE925B6ACE7296291B07D72ABFB794E3E6C89E93D868D9`.
- Latest protected backup: 14 files, verified-copy mismatches zero; manifest
  SHA-256 remains
  `8480B821CB2025C3847F8564264A596E0A57F96541590E8098FEB2EDAA5CAD1B`.
- Protected `V2 Test` staging: 50 files unchanged.
- X-Plane/xPilot processes: 0/0.
- Active/staged `.xpl`: 0/0.
- Active `win_x64`: absent.
- V1.2.3: untouched.

## Initial-plan audit

`INITIAL-PLAN WORKLOAD ACCEPTED AS CURRENT BASELINE — NO SAFE NARROW CHANGE PROPOSED`

No route or authority production change was made.

## Authorization boundary

No deployment, X-Plane/xPilot startup, live VATSIM contact, controlled live
reproof, or V1.2.3 modification occurred. A new Contract Gate and explicit
approval are required before any controlled live reproof.

Contract deviations: none.
