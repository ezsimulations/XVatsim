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

Step 3 history is process-memory only. Nothing is persisted to settings or disk,
and Step 3 does not connect a live METAR, ATIS, PDC, or private-message source.
Future sources must submit bounded facts through the brain's existing history
acceptance contract rather than writing directly to presentation state.
