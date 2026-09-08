# XVatsim V2 Step 4 METAR Correction — Offline Proof Summary

Date: 2026-08-28

## Result

**PASS — corrected and offline-proven. Controlled live reproof is not
authorized.**

The failed-live transport was caused by registering
`WINHTTP_CALLBACK_FLAG_SEND_REQUEST` while waiting for
`WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE`. The worker now registers the
matching `WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE`, observes that exact
completion, and only then begins response receipt.

## Corrective results

- Corrective scenarios: 14/14 passed.
- Complete Step 4 focused set: 67/67 passed.
- Complete saved suite: 561/561 passed.
- Canonical scenario fingerprint:
  `6AB913C1C0658A0CDE93A84D369267DDFDCD7FDE27797BF097FDB70DD574EF94`.
- Real WinHTTP loopback success: exact `/KDFW?format=json`, HTTP 200,
  complete payload, accepted matching JSON/station, worker harvest, one brain
  parse, VFR classification, and one history mutation in 17 ms.
- Real WinHTTP non-200 and malformed-JSON terminal ledgers: passed.
- Terminal diagnostic matrix: startup, send start, send completion, receive
  start, headers, HTTP, data availability, read, payload, JSON, wrong station,
  parser rejection, and success distinguished.
- Cooperative cancellation maximum: 15 ms; real stalled-response WinHTTP
  cancellation: 1 ms; limit: 500 ms.
- Lifecycle cleanup drain: terminal fact recorded and rejected with zero parse,
  cache/history mutation, presentation change, drawer/ORB effect, or scheduling.
- Warm unchanged runtime: 100,000 cycles with zero recurring parse,
  fingerprint, history, wrapping, raster, upload, publication, or terminal
  diagnostic work.
- Production-raster visuals: 15/15 generated; repeat comparison found zero PNG
  differences.
- Exact ORB strings: passed for neutral, fresh, cached, selected, and lookup
  states.
- Accepted main-card production signature remained
  `8949928878432326300` in every visual.

## Corrected ORB contract

- Startup, no target, pending, unavailable, and stale: one neutral gray line,
  `METAR`.
- Usable fresh or cached primary: exactly ICAO and VFR/MVFR/IFR/LIFR on two
  lines with the matching category tone.
- Selection is border-only. Lookup states never change ORB content.
- Detailed state and raw weather remain in the existing drawer.

## Release and isolation

- Fresh Release proof harness/visual build: passed.
- Fresh Release fixture-off normal plugin build: passed.
- Normal plugin size: 2,403,840 bytes.
- Normal plugin SHA-256:
  `2511F7BA9992893378423DE1F113CBAA04971DB7DD0F17505DA06FBCA0943288`.
- `metar.vatsim.net` and `VATSIM_METAR`: present.
- NOAA, Aviation Weather, loopback address, proof endpoint, injected transport,
  synthetic corrective METAR, and proof fixture markers: absent.

No plugin was deployed or staged. X-Plane and xPilot were not started. No live
VATSIM request was made. V1.2.3 was not modified.
