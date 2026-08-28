# XVatsim V2 Step 4 Accessory Liveness Correction Engineering Brief

Date: 2026-08-28

## Result

The second controlled-live reproof established that VATSIM transport, KDFW
parsing, repeated KABQ completion, and primary refresh remained healthy while
the accessory rail stopped accepting METAR, ATIS, and PDC actions. The failure
was reproduced offline before production code changed.

## Deterministic cause

An accepted click was bound to an exact selection generation and exact render
generation. An automatic METAR presentation update could advance only the
render generation before the matching draw. The draw then had the correct
selected drawer and selection generation but a newer render generation.
`AccessoryInputDispatchCoordinator` rejected it forever because the expected
render generation could never recur. The accepted action remained in flight,
and all later clicks were blocked behind it.

The unmodified red scenario observed:

- compatible generation-2 draw: `completed=false`;
- request identity returned as zero/none;
- dispatcher: `in_flight=true`; and
- subsequent action eligibility remained blocked.

Three adjacent paths were confirmed in the same shared boundary: failed
deferred binding could discard its identity without releasing the dispatcher;
lifecycle preparation stop did not clear deferred binding fields; and
performance action accounting required the same obsolete exact render
generation.

## Correction

The dispatcher now binds the resulting selected drawer as well as selection and
render generations. A draw has one of four bounded outcomes:

1. Exact drawer, selection, and render generation: exact completion.
2. Exact drawer and selection with a newer render generation: compatible
   supersession completion.
3. Newer selection generation: explicit selection-superseded cancellation.
4. Wrong drawer, older selection, or older render generation: mismatch without
   false completion.

Every terminal completion or cancellation releases the coordinator. Compatible
render supersession uses the same monotonic comparison in performance-action
accounting, preventing orphan timing records. Deferred state is cleared through
one lifecycle helper. Failure to bind a ready deferred presentation cancels the
matching in-flight request. Disable, stop, display close, and destruction leave
no deferred, queued, performance, or input-dispatch state able to publish later.

The queue and dispatcher expose bounded counters for produced, dropped,
consumed, discarded, pending, maximum depth, requests begun, blocked attempts,
presentations bound, exact completions, compatible supersessions, explicit and
selection-superseded cancellations, mismatches, maximum in-flight duration,
and final in-flight state. The plugin emits these only in its existing bounded
disable/stop aggregate diagnostic.

## METAR ORB readability

The locked minimal content contract is unchanged. Neutral states show only
`METAR`; usable primary weather shows only ICAO and category. Successful lines
now use Segoe UI Bold at 10.0 design pixels multiplied by layout scale, with
larger centered line boxes. Category tones and border-only selection are
unchanged.

## Proof

- Red Path B reproduced before the production correction.
- Corrective scenarios: 29/29.
- Step 3 focused scenarios: 31/31.
- Step 4 focused scenarios: 96/96.
- Complete saved suite: 590/590.
- Canonical scenario fingerprint:
  `765008264996DBE603FFB6B30D9CD0000B3CA59FA37BBA8C1D62D16CFD47E498`.
- Dispatcher stress: 1,000/1,000 compatible completions; queue zero; in-flight
  false; maximum synthetic action duration 1 microsecond.
- Warm unchanged proof: 100,000 cycles with zero parse, fingerprint, history,
  wrap, raster, upload, publication, input-dispatch, or diagnostic work.
- Visual proof: 22 production-raster PNGs, zero repeat differences, exact
  strings, unchanged main-card signature, and unclipped 0.85/1.0/1.35 scales.
- Successful real WinHTTP loopback lifecycle: 20 milliseconds.
- Cooperative cancellation: 15 milliseconds maximum; limit 500 milliseconds.
- Fresh fixture-off normal plugin SHA-256:
  `88BD236C879547F1C6BE386AB32BA8BDDEA50ACCF12A6960248B0A35DEB70080`.

No WinHTTP sequencing, endpoint, payload validation, parser, category,
targeting, cadence, cache, history, or flight-mode policy changed.

## Boundary

No deployment, X-Plane/xPilot startup, live VATSIM request, V1.2.3 change, or
controlled live reproof occurred. The fresh binary requires a separately
approved controlled-live-reproof Contract Gate.
