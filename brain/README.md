# Brain

This folder contains the central orchestration and decision layer.

The brain will own:

- canonical state
- orchestration loops
- module contracts
- decision logic
- render and action routing

## V2 Step 3 Accessory Ownership

The brain owns three independent in-memory histories for METAR, ATIS, and
PDC/private-message information while issuing at most one selected drawer for
the overlay to display. It decides open, close, and atomic switch transitions;
newest-first ordering; stable-key deduplication and replacement; count and byte
limits; oldest-first eviction; stable accepted sequences; generations; and the
lifecycle boundaries that preserve or clear the session histories.

The brain publishes immutable accessory preparation snapshots containing only
approved history data. Every snapshot and resulting presentation carries the
history, selection, layout, and identity generations required to reject stale
work. The below-normal preparation worker never reads mutable brain state and
never owns a parallel history or makes a selection, retention, or eviction
decision.

History is process-memory only. Nothing is persisted to settings or disk.

## V2 Step 4 METAR ownership

The brain owns the sole automatic primary airport, strict pilot-lookup
acceptance, request identity and priority, refresh and backoff eligibility,
completion rejection, parsing, category classification, observation freshness,
source health, cache, chronological history, spotlight deadlines, lifecycle,
and ORB/drawer presentation. `RunBrainOwnedAsyncFactCycle` is the generic
brain-owned dispatch seam; the plugin supplies only bounded facts and worker
bindings.

Content, source health, freshness, and presentation generations remain
separate. Unchanged raw content is not reparsed or republished. A successful
unchanged response may update source health, and only an actually visible state
change advances presentation work.

ATIS and PDC/private-message remain unconnected placeholders. They retain their
accepted Step 3 histories and are not synchronized or refactored by Step 4.
