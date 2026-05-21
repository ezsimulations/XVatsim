# Engineer 2 Contract Termination

Date terminated: 2026-05-19

Status: terminated as the primary runtime architecture.

Engineer 2 is the route-scoped, evidence-heavy authority engine that tried to
make XVatsim behave like a private radar map: plot the route, identify
route-relevant authority polygons, and prove live controllers against those
polygons through VATSpy, VATGlasses, transceiver geography, frequency ownership,
terminal data, and special-sector evidence.

That evidence model is still valuable as a verifier. It is no longer acceptable
as the primary live-loop discovery engine.

## Why It Is Terminated

The PANC -> KONT / UPS344 live battle test on 2026-05-19 proved that the
Engineer 2 runtime model still allows heavy proof work to enter the X-Plane
refresh path.

Measured current-run diagnostics:

- `927` current UPS344 diagnostic records were reviewed.
- `266` refreshes were at or above `50ms`.
- `43` refreshes were at or above `100ms`.
- `27` refreshes were at or above `250ms`.
- `15` refreshes were at or above `500ms`.
- Worst current-run authority proof: `879ms` in `AuthorityRelevance`.
- Worst current-run arrival refresh: `829ms`, including `737ms` in
  `ArrivalAirportCoverage` and `91ms` in overlay update.
- Other severe current-run spikes included `718ms`, `668ms`, `640ms`,
  `614ms`, and repeated `480ms+` authority proof events.

Functional failures from the same flight:

- `LAX_25_CTR 126.525` was correctly identified, proving the evidence engine
  can solve difficult center ownership.
- `LAX_S_DEP 124.300` was incorrectly displayed as an Arrival controller for
  `KONT`, proving terminal authority was not sufficiently destination-scoped.
- On touchdown at `KONT`, the route collapsed to `waypointList=ACFT` and
  `ROUTE unresolved`, which dropped `LAX_25_CTR` even though it was still
  online.

The conclusion is not that the evidence sources are useless. The conclusion is
that they are too expensive and too eager when used as the live discovery model
inside the simulator.

## Retired Assumptions

These assumptions are retired:

- XVatsim should continuously prove broad route authority from all live
  controller/feed evidence.
- Route movement should trigger full authority recomputation.
- Arrival preparation should run broad terminal or center discovery in the live
  refresh path.
- A scheduler alone can make heavy world-style proof safe inside X-Plane.
- The active route map should be the source of candidates.

## Preserved Pieces

These pieces remain useful and may be reused:

- Flight-plan parsing and route polygon construction.
- Active flight map / route-scoped polygon ordering.
- Typed `AuthorityEvidence` proof model.
- VATSpy, VATGlasses, transceiver geography, frequency ownership, terminal
  ownership, and special-sector source parsers.
- Rejection diagnostics.
- Regression harness scenarios that prove individual evidence paths.

The preserved pieces must now serve the Engineer 3 architecture as candidate
verifiers only. They must not self-trigger full live authority scans.

## Replacement Contract

The replacement contract is:

- `docs/RADIO_RANGE_AUTHORITY_GATE_CONTRACT.md`

Engineer 3 starts from radio-reachable controllers first. XVatsim only verifies
controllers that the pilot can actually reach or controllers that are
destination-targeted during the 200nm Arrival preparation window.
