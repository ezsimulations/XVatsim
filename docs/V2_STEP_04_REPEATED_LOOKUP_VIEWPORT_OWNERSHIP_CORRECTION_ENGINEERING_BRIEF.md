# XVatsim V2 — Step 4 Repeated-Lookup Viewport Ownership Correction

## Scope and diagnosis

The controlled-live C2 failure was a viewport-ownership defect, not a request,
parser, history, preparation-binding, or publication failure. The Brain accepted
the second byte-identical KABQ report without parsing or mutating history and
published a new lookup spotlight command. Because the METAR drawer was already
open, the existing selection-only scroll reset did not advance. The presenter
therefore retained the user's nonzero offset and rendered the new spotlight
above the visible viewport.

The unchanged production seam reproduced the same failure. With the drawer at
offset 7, the pending, identical spotlight, and expiry commands terminally
published while the visible first line remained 5. The spotlight was absent
from the rendered visible lines. Preparation binding, queue delivery, Brain
terminal acceptance, and liveness accounting all remained successful.

## Correction

The existing Brain-owned `scrollResetGeneration` remains the sole semantic
viewport-reset identity.

- The immutable `BrainOwnedAccessoryPresentationSnapshot` now carries the
  generation with its lifecycle and command identity.
- A currently owned METAR lookup advances it once for pending, spotlight,
  failure, spotlight/failure expiry, and legitimate primary preemption.
- Selection continues to advance it once through the existing selection path;
  the lookup path avoids a second increment when selection itself changed.
- Lost lookup ownership and ordinary background history/primary mutation do
  not advance it.
- The overlay compares the exact committed snapshot generation with the last
  applied generation, resets the drawer to line 0 before rasterization, and
  consumes that generation once with that command.
- Mechanical repreparation and unchanged frames cannot consume the generation
  again. A stale or superseded snapshot cannot reset the current presenter.
- Publication diagnostics correlate snapshot/applied reset generations,
  before/after offsets, reset application, command/lifecycle identity, and the
  first visible line on the qualifying frame.

No worker, queue, timer, retry loop, parser rule, history rule, transport rule,
refresh cadence, spotlight duration, or semantic preparation path was added or
changed.

## Production-seam coverage

Five unique scenarios were added:

1. Scrolled METAR plus repeated lookup pending resets to the top.
2. Identical KABQ completion resets to and visibly presents the spotlight.
3. Spotlight expiry restores pinned KDFW at the top.
4. ATIS ownership blocks a late METAR reset and drawer theft.
5. Ordinary background METAR mutation preserves a nonzero user scroll offset.

They traverse the Brain mutation, immutable projection, generic preparation
worker, exact-plan binding, presenter, drawer render plan, visible-publication
coordinator, production publication queue, Brain consumer, and production
diagnostic serializer. No expected fact, viewport result, or success ledger is
injected.

## Measured result

- Red: pending/spotlight/expiry retained offsets and failed visibility.
- Green: reset generations advance once, offsets become 0, and the intended
  first visible line is 0.
- Identical KABQ: parse delta 0; history-mutation delta 0.
- Lost ownership: ATIS remains selected; late snapshots/publications 0.
- Background mutation: offset remains 7; reset generation unchanged.
- Ten unchanged spotlight frames: raster 0; publication 0.
- Relevant Step 4: 264/264.
- Complete regression: 764/764.

This work is offline proof only. It did not start an application, deploy a
payload, or make a live request.
