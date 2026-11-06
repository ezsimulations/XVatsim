# Experiment contract

## Invariants

1. Source facts are never converted into numerical votes.
2. An online controller may own zero, one, or many polygons.
3. A route may traverse overlapping Center and terminal polygons.
4. A callsign suffix may match source metadata; it may not veto a direct live
   ownership assignment.
5. Distance from aircraft or transmitter is not an ownership input.
6. Flight phase is not an ownership input.
7. Unmapped and conflicting facts remain visible in output.
8. Geometry work is route-scoped after the static spatial index is built.
9. Every result identifies the source facts that produced it.
10. Controller refreshes are complete snapshots. Added and changed controllers
    are evaluated; removed controllers retire their prior claims; unchanged
    controllers retain their claims without re-evaluation.
11. Route distance is reported to consumers but never suppresses an online
    owner of a polygon traversed by the route.

## First feasibility gate

The UPS344 PANC-KONT experiment must prove all of the following before any
production rewrite is proposed:

- The route crosses the expected Anchorage, Vancouver, Seattle, Oakland, Los
  Angeles, and SoCal geometry available in the public datasets.
- Nested terminal and Center intersections survive as separate crossings.
- The live `LAX_S_DEP 124.300` vNAS position can light its matching SoCal
  polygon without an APP/DEP phase rule.
- A controller whose owned polygon does not intersect the route cannot enter
  the lit-route result.
- Replaying captured inputs is deterministic.
- Initial catalog construction and incremental ownership joins are timed
  independently.
- Reapplying an identical controller snapshot changes zero polygons and runs
  zero controller ownership evaluations.
- Incremental ownership claims exactly match a fresh batch join after staged
  online/offline changes.

Failure must be classified as one of: missing geometry, missing ownership
crosswalk, conflicting source facts, route defect, or implementation defect.
No fallback vote may conceal the category.

