# XVatsim V2 Step 4 Correction Engineering Brief

## Disposition

The Step 4 VATSIM METAR correction is implemented and offline-proven. It fixes
the failed controlled-live transport completion and simplifies only the METAR
ORB. Deployment, X-Plane/xPilot startup, live VATSIM traffic, and controlled
live reproof remain unauthorized.

The governing architecture remains:

`Brain decides. Modules produce facts. UI displays brain-approved facts.`

## Confirmed failure and correction

The production worker formerly registered
`WINHTTP_CALLBACK_FLAG_SEND_REQUEST` and then waited for
`WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE`. The awaited completion event
was therefore never registered. The corrected worker registers
`WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE`, waits for that exact event, and
only then calls `WinHttpReceiveResponse`.

The existing HTTPS-only `metar.vatsim.net` host, exact single-airport path,
redirect rejection, timeouts, response bounds, station validation, JSON shape,
raw METAR limits, cancellation event, callback lifetime guard, and one-worker
limit remain intact.

## Terminal transport and brain diagnostics

Each dispatched request produces three bounded one-shot records:

1. Dispatch: request ID, purpose, ICAO, both generations, and dispatch time.
2. Terminal worker: the same identity, terminal stage, worker status, bounded
   reason, WinHTTP operation/result/error, HTTP status, payload bytes, network
   elapsed time, completion monotonic time, progress bits, and source.
3. Brain disposition: accepted/rejected, parser attempt and reason, accepted
   category, history mutation, presentation change, and disposition reason.

The terminal stages distinguish startup, send start, send completion, receive
start, response headers, HTTP status, data availability, read, payload, JSON,
station identity, and successful completion. Diagnostics contain no response
body, raw METAR, credentials, account identity, private messages, or unrelated
simulator data. Idle cycles emit no worker-busy or repeated terminal lines.

On disable or stop, cancellation closes the request and joins the worker. A
terminal fact drained afterward is recorded and explicitly rejected as cleanup
evidence. It is never parsed or accepted, never mutates cache/history, never
changes or reopens the drawer or ORB, and never schedules new work.

## Real WinHTTP offline proof

The proof-only module target connects only to a loopback peer bound to
`127.0.0.1` on an ephemeral port. The peer verifies
`/KDFW?format=json` and provides bounded success, non-200, malformed-JSON, and
stalled-response cases. The successful case exercises the production WinHTTP
state machine through send completion, response headers, HTTP 200, complete
payload read, JSON/station acceptance, worker harvest, one brain parse, VFR
classification, history publication, and complete worker/callback shutdown.

The proof endpoint seam is compile-time isolated in `XVatsimMetarProof`. The
normal fixture-off plugin contains no loopback endpoint, proof transport,
synthetic correction METAR, or alternate weather source.

## Minimal METAR ORB

The brain projects exactly two forms:

- No usable primary—startup, no target, pending, unavailable, or stale:
  neutral gray `METAR` on one centered line.
- Usable fresh or still-fresh cached primary: ICAO and exact category on two
  centered lines, with green VFR, blue MVFR, red IFR, or magenta LIFR tone.

`UNKNOWN`, `UNAVAILABLE`, `PENDING`, `FRESH`, `CACHED`, `STALE`, `PRIMARY`,
`OPEN`, ages, times, source, and raw METAR never appear inside the ORB. The
selected state is border-only. Lookup pending, spotlight, failure, expiry,
manual close, and drawer switching never change the primary ORB. Detailed state
continues to live in the existing METAR drawer.

ATIS, PDC, the main card, targeting, lookup rules, cadence, failure backoff,
parser thresholds, freshness, history, disconnect behavior, and lifecycle
policy are otherwise unchanged.

## Proof inventory

- Corrective scenarios: 14/14.
- Complete Step 4 focused set: 67/67.
- Complete saved suite: 561/561.
- Fifteen production-raster visuals, including the minimal ORB state matrix.
- Repeat render comparison with zero PNG differences.
- Exact presentation-string assertions and unchanged accepted main-card
  production signature.
- 100,000 unchanged cycles with zero parse, fingerprint, history, wrapping,
  raster, upload, publication, or terminal-diagnostic work.
- Cooperative and real-WinHTTP stalled cancellation within 500 milliseconds.
- Fresh Windows Release proof and fixture-off normal builds.
- Normal-binary VATSIM-source presence and proof/alternate-source isolation.

Evidence is recorded under the new
`outputs/v2_step_04_metar_correction_*` paths. Both earlier protected evidence
sets remain byte-identical and excluded from the corrective commits.
