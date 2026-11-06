# UPS344 PANC-KONT first experiment

Date: 2026-10-28

## Result

The route-scoped ownership model is feasible on the captured UPS344 flight.
It produced an ordered airspace map without vote totals, aircraft/controller
distance gates, airport aliases, or flight-phase controller elimination.

Captured result:

- 27 FMS route points.
- 3,084 route-relevant catalog polygons after loading only matching authority
  packages.
- 36 route/airspace crossings after altitude-aware VATGlasses selection.
- 15 lit route polygon records.
- 54 operational online controller facts after excluding non-controller VATSIM
  clients.
- One unresolved operational controller mapping (`SVMI_APP`), outside this
  flight's route.
- One route ownership conflict on the broad Vancouver FIR parent polygon;
  detailed Vancouver child polygons resolve to `CZVR_CTR 133.700`.
- One malformed public source polygon repaired explicitly and recorded.

## SoCal proof

`LAX_S_DEP 124.300` was present in all three captured live facts:

- VATSIM online controller roster.
- vNAS live controller feed, with radio name `SoCal Approach`.
- XVatsim/xPilot radio-board diagnostics.

The vNAS position assumed TCP sectors `6S`, `6L`, `6N`, `6X`, and `6K`. The ZLA
facility graph resolves `6L` to `LAX_L_APP` and `6K` to `LAX_K_APP`. Both map to
the SimAware `SCT/LAX/APP` polygon. The PANC-KONT route intersects that polygon
from route mile 2104.5 to 2108.9.

Therefore the experiment lights:

`SCT SoCal Approach -> LAX_S_DEP 124.300`

No rule inspected the flight phase or rejected the controller because its login
callsign ended in `_DEP`.

## Center subdivision proof

The experiment selected VATGlasses packages only for route-crossed regions.
Available detail packages were found for Vancouver, Seattle, and Oakland.

While `SEA_16_CTR 135.450` was online, the captured route lit the broad KZSE
parent and the detailed child sectors encountered in order, including sectors
3, 2, 32, 16, and 13. This demonstrates that parent Center and subdivided Center
geometry can coexist without a winner-take-all filter.

## Explicit data gaps

The source registry did not provide detailed VATGlasses subdivision packages
for route regions KZLA, PAZA, or PAZAD. Their broad FIR geometry remains
available, and SoCal terminal geometry remains available, but detailed Center
subsector parity cannot be claimed from the current sources.

This is a data-coverage gap, not a voting or route-intersection problem. The
experiment reports it in `source_manifest.route_authority.missing_route_regions`.

## Determinism and performance

Two replays of the same captured snapshots produced identical evidence digest:

`ce309994c3ea4dab1824dcf54d0699000cabebf554185dbfc02d1f0e9284776e`

Warm-cache timing on this machine:

- Static source acquisition: 4.465 ms.
- Base catalog build: 214.092 ms.
- Base route intersection: 4.034 ms.
- Route-authority source acquisition: 3.829 ms.
- Route-authority catalog build: 25.154 ms.
- Final route intersection: 14.182 ms.
- Captured live-fact load: 87.288 ms.
- Ownership join: 26.561 ms.
- Total standalone replay: 386.684 ms.

All of this currently runs as a standalone process. No claim is made yet about
production X-Plane flight-loop timing. The result does show that route
intersection and ownership join can be isolated, cached, and performed without
repeated 59,244-polygon flight-loop scans.

## Verification

- Eight isolated unit tests pass, including nested geometry retention, exact
  VATGlasses controller patterns, vNAS assumed-TCP ownership, no-distance-gate
  route state, incremental online/offline updates, overlapping APP/DEP
  retention, and explicit same-polygon conflicts.
- Python compile check passes.
- Interactive map and CSV/JSON evidence were rendered successfully.
- No production XVatsim source, build configuration, XPL, or X-Plane file was
  modified.

## Next battle tests

1. Replay KNGU-EDFH to prove Potomac polygons remain unlit when the route does
   not intersect them.
2. Replay Vancouver entry timing with Center child polygons and no range gate.
   Static route-state proof is complete; a live aircraft-position replay remains.
3. Capture controllers coming online and offline and prove an incremental
   ownership update changes only affected polygons. Captured staged replay is
   complete; a live-feed soak test remains.
4. Test overlapping APP/DEP ownership and explicit conflicts without votes.
   Synthetic contract tests are complete; a captured live overlap remains.
5. Audit missing Center-subdivision sources, especially ZLA and PAZA.
6. Add an aircraft-progress cursor that reports the current active polygon set
   and the next polygon-entry event without recalculating the route. The pure
   progress query is complete; simulator-position integration remains.

