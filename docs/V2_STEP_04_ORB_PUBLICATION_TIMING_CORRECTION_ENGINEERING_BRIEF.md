# V2 Step 4 Automatic ORB Publication And Timing Correction

## Disposition

This correction is offline-only. It does not authorize deployment, X-Plane or
xPilot startup, or live VATSIM traffic.

## Reproduced failures

Two deterministic red cases were preserved before production edits.

1. A closed accessory rail retained its neutral `METAR` texture after the brain
   accepted KDFW/VFR. The projected snapshot and stored content signature were
   current, but content-only presentation changes did not dirty the rail. A
   later METAR selection changed selection state, requested a rail raster, and
   exposed the already accepted KDFW/VFR content.
2. Deferred accessory preparation contributed to end-to-end wall time, but the
   shared deferred-binding clear path erased the preparation-wait timestamp
   before binding. The collector consequently reported zero preparation wait
   and misclassified the whole deferred interval as synchronous dispatch work.

Both cases failed against the unmodified `ac1356d` production implementation.

## Correction

The overlay now compares a rendered-rail signature made only from fields that
can change rail pixels: rail dimensions and scale, visibility, per-ORB drawer,
tone, selected/neutral state, relative geometry, visible label or ICAO/category,
and selected indicator. A changed signature requests exactly one rail raster
and upload. Presentation generation alone does not dirty the rail, so drawer
history, lookup spotlight, age, and identical-primary changes remain rail-zero.

Deferred-binding state and timing ownership are separate. Timing is retained
until successful generation binding or an explicit terminal cancellation. The
recorded timeline includes mouse acceptance, dispatch, preparation request,
worker start/end, publication, ready collection, binding, matching draw, and
terminal completion. Non-overlapping durations distinguish queue wait, worker
CPU, publication handoff, publication-to-collection wait, ready-to-bind wait,
synchronous dispatch, frame wait, and draw work. Unattributed and overlapping
time are reported explicitly.

Changed accepted METAR content now records the parser/classifier elapsed time
and confirms that it ran on the permitted simulator flight-loop harvest path.
Identical content reports no parse and zero parser elapsed time.

## Preserved boundaries

No route, authority, controller relevance, WinHTTP, endpoint, transport,
payload validation, METAR parsing policy, category threshold, targeting,
history, lookup, cache, or refresh-cadence behavior changed. The accepted
10.0-design-pixel METAR ORB typography is unchanged.

## Proof contract

- 40 corrective scenarios cover the red reproductions, the complete ORB
  transition matrix, exact rail/drawer work, timing phase ownership, action
  classification, lifecycle cancellation, and parser timing.
- Existing Step 3, Step 4, stress, quiet-runtime, worker shutdown, and real
  WinHTTP loopback proofs remain mandatory.
- The normal plugin is fixture-off and must contain only the production VATSIM
  METAR source markers.
