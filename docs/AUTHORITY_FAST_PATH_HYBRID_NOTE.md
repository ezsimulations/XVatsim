# Authority Fast-Path Hybrid Performance Note

## Purpose

Protect low-end X-Plane machines from authority proof CPU spikes by using a cheap, route-scoped discovery pass first, then reserving the expensive evidence engine for unresolved polygons only.

## Core Principle

The fast path may accept obvious route-relevant authority matches, but it may never reject or permanently dismiss a controller or polygon.

Old/simple logic becomes a scout, not the judge.

## Proposed Flow

1. When a flight plan is received, build the route-relevant authority polygon list.
2. Run a cheap fast-path sweep against route-scoped active controllers using published patterns, facility, and frequency clues.
3. Cache any high-confidence matches as `FAST_PATH_ACCEPTED`.
4. Mark unmatched route polygons as `UNRESOLVED_PENDING`, not empty or rejected.
5. Do not immediately run the heavy evidence engine across every unresolved polygon.
6. As the aircraft approaches the 200nm authority window, run the full evidence engine only for unresolved nearby polygon groups.
7. If the full engine proves a controller, cache it as `SLOW_PATH_PROVED`.
8. If the full engine confirms no relevant controller, cache it as `EMPTY_CONFIRMED` with a safe recheck trigger.
9. Recheck cached empty/unresolved polygons only when the controller feed, transceiver feed, route window, or source generation changes.

## Safety Rules

- The authority evidence contract remains the only final truth source for uncertain cases.
- Fast path cannot block later frequency, transceiver, VATGlasses, VATSpy, SimAware/TRACON, special-sector, or duplicated-ATIS proof.
- A string/name mismatch alone cannot reject a controller.
- Every accepted controller must still carry diagnostics showing the proof path.
- The UI should only display proven active/relevant controllers, never raw unresolved guesses.

## Expected Benefit

Most normal flights should resolve common center/terminal controllers through the cheap path with minimal CPU cost. The expensive proof engine should wake only when it has work that matters, such as suffix mismatches, frequency-owned rescue, transceiver geometry, special-sector ownership, or unresolved polygons inside the useful 200nm window.

## Working Labels

- `FAST_PATH_ACCEPTED`
- `UNRESOLVED_PENDING`
- `SLOW_PATH_PROVED`
- `EMPTY_CONFIRMED`

## Status

Discussion note only. Do not implement until the current cache/performance build is flight-tested and we decide this needs to become the next formal performance contract.
