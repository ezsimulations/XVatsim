# V2 Step 4 Brain-Exclusive Accessory Authority Offline Proof

Date: 2026-08-28

## Result

All authorized offline implementation and proof requirements passed.

- Brain-exclusive corrective scenarios: 44/44.
- Step 3 focused scenarios: 31/31.
- Complete Step 4 focused scenarios: 180/180.
- Complete saved suite: 674/674.
- Scenario fingerprint:
  `EE5B15EAE725EBA23AEA1CE9F9FEB82D0D93578C0A18EBA6129AE8B898E645C3`.

## Reproduced causes

Before correction, deterministic red cases proved:

- an empty hidden METAR preparation could outlive accepted KDFW content;
- competing normal-update and draw-time commit paths could leave a render-bound
  action permanently in flight; and
- lifecycle cancellation could conceal a greater-than-500-ms liveness failure.

After correction, the exact startup/accept/open/close/reopen sequence always
uses the latest brain command, all scheduling orderings converge on that
command, later clicks never wait for drawing, and cancellation remains
truthfully timed.

## Authority proof

- One existing METAR worker performs unchanged bounded transport and calls one
  stateless payload-only decoder.
- The decoder has no requested airport or acceptance output and owns no worker,
  mailbox, scheduler, deadline, or product state.
- The worker returns one composite transport/decode fact.
- The brain alone accepts station/report content, authorizes parsing, commits
  classification/cache/history/source health, chooses drawer/spotlight/ORB
  state, and schedules later work.
- One immutable brain presentation command carries exact command identity,
  lifecycle epoch, rail revision, drawer revision, selection, drawer content,
  and shared immutable history content.
- `OverlayWindow::UpdateAccessory` is the only production presentation commit
  coordinator. No draw-time commit remains.
- Pilot facts are consumed directly into brain decisions and never wait for a
  render acknowledgement.
- Every issued command receives one bounded terminal mechanical fact. Terminal
  capacity is reserved before issue and facts cannot be overwritten or
  coalesced.

## Stress and idle

The 1,000-action proof ended with 1,000 terminal facts, zero dropped input,
zero queued input, zero pending publication, no behavioral in-flight action,
100-microsecond deterministic maximum command time, and zero liveness failures.

The 100,000-cycle unchanged proof recorded zero history visits, copies,
preparations, wrapping, rail raster, drawer raster, upload, publication, input
dispatch, or diagnostic work.

## Transport and lifecycle regression

The real WinHTTP loopback success path completed `/KDFW?format=json`, decoded
one matching report, reached one brain parse, and completed in 17 ms. The
cooperative shutdown proof exercised five phases with a 15 ms maximum, below
the 500 ms limit. The loopback stalled-cancellation observation was 0 ms.

No live VATSIM request was made.

## Visual proof

Two complete production-raster runs generated 33 PNG files each with zero PNG
hash differences. Exact ORB strings, approved typography, selected state,
first populated drawer, spotlight/manual ownership, and primary transitions
were retained. The accepted main-card production signature remained unchanged.

The transition ledger records:

- one rail raster and one upload for neutral-to-KDFW, category, KDFW-to-KSAN,
  and successful-to-neutral pixel changes;
- drawer work only for a KABQ spotlight whose primary ORB is unchanged; and
- zero rail/drawer raster or upload for identical primary content.

## Build and isolation

Fresh Visual Studio 18 2026 x64 Release proof and fixture-off normal builds
passed with the established cpprestsdk coroutine compatibility definition.

Normal plugin:

- bytes: 2,422,784;
- SHA-256:
  `F6C5B2B35CBBDB83BCE1FBD6D9A8029F3E7A258BFC4B420F8F87A208FCFCB13C`;
- contains `metar.vatsim.net` and `VATSIM_METAR`;
- contains no NOAA, Aviation Weather, loopback, injected transport, synthetic
  corrective METAR, proof endpoint, scenario-name, or proof-fixture marker.

## Boundaries

`INITIAL-PLAN WORKLOAD ACCEPTED AS CURRENT BASELINE — NO SAFE NARROW CHANGE PROPOSED`

No deployment, X-Plane/xPilot startup, live VATSIM contact, route/authority
optimization, V1.2.3 modification, or controlled live proof occurred.

Contract deviations: none.
