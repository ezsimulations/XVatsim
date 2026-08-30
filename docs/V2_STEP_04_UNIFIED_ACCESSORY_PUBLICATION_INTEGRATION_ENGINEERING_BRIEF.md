# Step 4 Unified Accessory Publication Integration — Engineering Brief

Date: 2026-08-29

## Approved design

Stage One defined command-terminal-only, visible-attempt-terminal-only, and
combined facts; independent lifecycle-scoped ordering domains; one mechanical
publication coordinator; atomic Brain consumption; retained immutable terminal
delivery; lifecycle-first cancellation delivery; and decision-derived
diagnostics. The Director approved that design with binding lifecycle,
event-latched retry, and named visibility-loss clarifications. Darron explicitly
authorized Stage Two.

## Red reproduction

Before correction, all five focused behavioral cases failed:

1. same-command visible attempt rejected after hidden command terminal;
2. combined fact advanced only the command domain;
3. legacy and new coordinators could represent competing attempts;
4. a full queue left a completed attempt without retained delivery; and
5. a dequeued Brain rejection was counted as terminal success.

## Correction

The queue now validates command and visible-attempt roles independently within
the lifecycle epoch and advances combined roles atomically. The Brain performs
the same two-dimensional validation and evaluates applicable timing intervals
with at most one aggregate liveness failure per physical fact.

`BeginAccessoryVisiblePublication()` and its independent counter and awaiting
fields were removed. `AccessoryVisiblePublicationState` is the sole coordinator
used by `OverlayWindow` for eligibility, attempt identity, visibility epoch,
draw completion, texture failure, supersession, and visibility loss. Hidden
command facts and visibility-loss facts are built by production-owned helpers.

If the bounded publication queue is full, the coordinator retains the exact
immutable fact, requests the existing service path once, prevents replacement,
and retries after a dequeue frees capacity. It performs no draw polling and no
idle work after delivery. Orderly disable hides and terminally accounts before
the Brain lifecycle advances; forced late facts are explicitly stale-rejected.

Plugin accounting now distinguishes dequeued, Brain-accepted, Brain-rejected,
command-terminal, attempt-terminal, combined, and stale-rejected facts. The
plugin and harness use the same production diagnostic serializer.

## Preserved behavior

No METAR transport, source authority, parser, category, KDFW/KABQ/KSAN
targeting, ORB content, lookup ownership, history, refresh cadence, Arrival,
V1.2.3, or protected evidence behavior was changed.
